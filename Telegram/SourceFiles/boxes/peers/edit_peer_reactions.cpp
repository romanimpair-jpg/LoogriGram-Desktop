/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/peers/edit_peer_reactions.h"

#include "apiwrap.h"
#include "base/event_filter.h"
#include "chat_helpers/emoji_list_widget.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/ui_integration.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_document.h"
#include "data/data_peer_values.h" // UniqueReactionsLimit.
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtWidgets/QTextEdit>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocumentFragment>

namespace {

// LoogriGram: a channel could also take custom emoji as reactions, as many
// as its boost level allowed, the rest drawn dimmed. Boosts come from
// Premium subscribers, so channels offer the standard reactions only, like
// groups, and a custom emoji pasted into the field is simply dropped.
struct UniqueCustomEmojiContext {
	std::vector<DocumentId> ids;
	Fn<bool(DocumentId)> isCustomReaction;
};

[[nodiscard]] QString AllowOnlyCustomEmojiProcessor(QStringView mimeTag) {
	auto all = TextUtilities::SplitTags(mimeTag);
	for (auto i = all.begin(); i != all.end();) {
		if (Ui::InputField::IsCustomEmojiLink(*i)) {
			++i;
		} else {
			i = all.erase(i);
		}
	}
	return TextUtilities::JoinTag(all);
}

[[nodiscard]] bool AllowOnlyCustomEmojiMimeDataHook(
		not_null<const QMimeData*> data,
		Ui::InputField::MimeAction action) {
	if (action == Ui::InputField::MimeAction::Check) {
		const auto textMime = TextUtilities::TagsTextMimeType();
		const auto tagsMime = TextUtilities::TagsMimeType();
		if (!data->hasFormat(textMime) || !data->hasFormat(tagsMime)) {
			return false;
		}
		auto text = QString::fromUtf8(data->data(textMime));
		auto tags = TextUtilities::DeserializeTags(
			data->data(tagsMime),
			text.size());
		auto checkedTill = 0;
		ranges::sort(tags, ranges::less(), &TextWithTags::Tag::offset);
		for (const auto &tag : tags) {
			if (tag.offset != checkedTill
				|| AllowOnlyCustomEmojiProcessor(tag.id) != tag.id) {
				return false;
			}
			checkedTill += tag.length;
		}
		return true;
	} else if (action == Ui::InputField::MimeAction::Insert) {
		return false;
	}
	Unexpected("Action in MimeData hook.");
}

[[nodiscard]] std::vector<Data::ReactionId> DefaultSelected() {
	const auto like = QString::fromUtf8("\xf0\x9f\x91\x8d");
	const auto dislike = QString::fromUtf8("\xf0\x9f\x91\x8e");
	return { Data::ReactionId{ like }, Data::ReactionId{ dislike } };
}

[[nodiscard]] std::vector<Data::ReactionId> CollectAvailableReactions(
		not_null<Main::Session*> session) {
	const auto &all = session->data().reactions().list(
		Data::Reactions::Type::Active);
	if (all.empty()) {
		return DefaultSelected();
	}
	auto result = std::vector<Data::ReactionId>();
	result.reserve(all.size());
	for (const auto &reaction : all) {
		result.push_back(reaction.id);
	}
	return result;
}

[[nodiscard]] bool RemoveNonCustomEmojiFragment(
		not_null<QTextDocument*> document,
		UniqueCustomEmojiContext &context) {
	context.ids.clear();
	auto removeFrom = 0;
	auto removeTill = 0;
	auto block = document->begin();
	for (auto j = block.begin(); !j.atEnd(); ++j) {
		const auto fragment = j.fragment();
		Assert(fragment.isValid());

		removeTill = removeFrom = fragment.position();
		const auto format = fragment.charFormat();
		if (format.objectType() != Ui::InputField::kCustomEmojiFormat) {
			removeTill += fragment.length();
			break;
		}
		const auto id = format.property(Ui::InputField::kCustomEmojiId);
		const auto documentId = id.toULongLong();
		if (ranges::contains(context.ids, documentId)
			|| context.isCustomReaction(documentId)) {
			removeTill += fragment.length();
			break;
		}
		context.ids.push_back(documentId);
	}
	while (removeTill == removeFrom) {
		block = block.next();
		if (block == document->end()) {
			return false;
		}
		removeTill = block.position();
	}
	Ui::PrepareFormattingOptimization(document);
	auto cursor = QTextCursor(document);
	cursor.setPosition(removeFrom);
	cursor.setPosition(removeTill, QTextCursor::KeepAnchor);
	cursor.removeSelectedText();
	return true;
}

bool RemoveNonCustomEmoji(
		not_null<QTextDocument*> document,
		UniqueCustomEmojiContext &context) {
	if (!RemoveNonCustomEmojiFragment(document, context)) {
		return false;
	}
	while (RemoveNonCustomEmojiFragment(document, context)) {
	}
	return true;
}

void SetupOnlyCustomEmojiField(
		not_null<Ui::InputField*> field,
		Fn<void(std::vector<DocumentId>)> callback,
		Fn<bool(DocumentId)> isCustomReaction) {
	field->setTagMimeProcessor(AllowOnlyCustomEmojiProcessor);
	field->setMimeDataHook(AllowOnlyCustomEmojiMimeDataHook);

	struct State {
		bool processing = false;
		bool pending = false;
	};
	const auto state = field->lifetime().make_state<State>();

	field->changes(
	) | rpl::on_next([=] {
		state->pending = true;
		if (state->processing) {
			return;
		}
		auto context = UniqueCustomEmojiContext{
			.isCustomReaction = isCustomReaction,
		};
		auto changed = false;
		state->processing = true;
		while (state->pending) {
			state->pending = false;
			const auto document = field->rawTextEdit()->document();
			const auto pageSize = document->pageSize();
			QTextCursor(document).joinPreviousEditBlock();
			if (RemoveNonCustomEmoji(document, context)) {
				changed = true;
			}
			state->processing = false;
			QTextCursor(document).endEditBlock();
			if (document->pageSize() != pageSize) {
				document->setPageSize(pageSize);
			}
		}
		callback(context.ids);
		if (changed) {
			field->forceProcessContentsChanges();
		}
	}, field->lifetime());
}

[[nodiscard]] TextWithTags ComposeEmojiList(
		not_null<Data::Reactions*> reactions,
		const std::vector<Data::ReactionId> &list) {
	auto result = TextWithTags();
	const auto size = [&] {
		return int(result.text.size());
	};
	auto added = base::flat_set<Data::ReactionId>();
	const auto &all = reactions->list(Data::Reactions::Type::All);
	const auto add = [&](Data::ReactionId id) {
		if (!added.emplace(id).second) {
			return;
		}
		auto unifiedId = id.custom();
		const auto offset = size();
		if (unifiedId) {
			result.text.append('@');
		} else {
			result.text.append(id.emoji());
			const auto i = ranges::find(all, id, &Data::Reaction::id);
			if (i == end(all)) {
				return;
			}
			unifiedId = i->selectAnimation->id;
		}
		const auto data = Data::SerializeCustomEmojiId(unifiedId);
		const auto tag = Ui::InputField::CustomEmojiLink(data);
		result.tags.append({ offset, size() - offset, tag });
	};
	for (const auto &id : list) {
		add(id);
	}
	return result;
}

enum class ReactionsSelectorState {
	Active,
	Disabled,
	Hidden,
};

struct ReactionsSelectorArgs {
	not_null<QWidget*> outer;
	not_null<Window::SessionController*> controller;
	rpl::producer<QString> title;
	std::vector<Data::Reaction> list;
	std::vector<Data::ReactionId> selected;
	Fn<void(std::vector<Data::ReactionId>)> callback;
	rpl::producer<ReactionsSelectorState> stateValue;
	bool all = false;
	bool isGroup = false;
};

object_ptr<Ui::RpWidget> AddReactionsSelector(
		not_null<Ui::RpWidget*> parent,
		ReactionsSelectorArgs &&args) {
	using namespace ChatHelpers;
	using HistoryView::Reactions::UnifiedFactoryOwner;

	auto result = object_ptr<Ui::InputField>(
		parent,
		st::manageGroupReactionsField,
		Ui::InputField::Mode::MultiLine,
		std::move(args.title));
	const auto raw = result.data();
	const auto session = &args.controller->session();
	const auto owner = &session->data();
	const auto reactions = &owner->reactions();

	struct State {
		std::unique_ptr<Ui::RpWidget> overlay;
		std::unique_ptr<UnifiedFactoryOwner> unifiedFactoryOwner;
		UnifiedFactoryOwner::RecentFactory factory;
		std::vector<Data::ReactionId> reactions;
		rpl::lifetime focusLifetime;
	};
	auto normal = reactions->list(Data::Reactions::Type::Active);
	const auto state = raw->lifetime().make_state<State>();
	state->unifiedFactoryOwner = std::make_unique<UnifiedFactoryOwner>(
		session,
		normal);
	state->factory = state->unifiedFactoryOwner->factory();
	state->reactions = std::move(args.selected);

	const auto customEmojiPaused = [controller = args.controller] {
		return controller->isGifPausedAtLeastFor(PauseReason::Layer);
	};
	auto simpleContext = Core::TextContext({
		.session = session,
		.repaint = [=] { raw->update(); },
	});
	auto context = simpleContext;
	context.customEmojiFactory = [=](
		QStringView data,
		const Ui::Text::MarkedContext &context
	) -> std::unique_ptr<Ui::Text::CustomEmoji> {
		using namespace Ui::Text;
		return MakeWrappedEmoji<FirstFrameEmoji>(
			MakeCustomEmoji(data, simpleContext));
	};
	raw->setCustomTextContext(
		std::move(context),
		customEmojiPaused,
		customEmojiPaused);

	const auto callback = args.callback;
	const auto isCustom = [=](DocumentId id) {
		return state->unifiedFactoryOwner->lookupReactionId(id).custom();
	};
	SetupOnlyCustomEmojiField(raw, [=](std::vector<DocumentId> ids) {
		auto reactions = std::vector<Data::ReactionId>();
		reactions.reserve(ids.size());
		const auto owner = state->unifiedFactoryOwner.get();
		for (const auto id : ids) {
			reactions.push_back(owner->lookupReactionId(id));
		}
		state->reactions = reactions;
		callback(std::move(reactions));
	}, isCustom);
	const auto applyFromState = [=] {
		raw->setTextWithTags(ComposeEmojiList(reactions, state->reactions));
	};

	applyFromState();

	const auto toggle = Ui::CreateChild<Ui::IconButton>(
		parent.get(),
		st::manageGroupReactions);

	using SelectorState = ReactionsSelectorState;
	std::move(
		args.stateValue
	) | rpl::on_next([=, all = args.all](SelectorState value) {
		switch (value) {
		case SelectorState::Active:
			state->overlay = nullptr;
			state->focusLifetime.destroy();
			if (raw->empty()) {
				raw->setTextWithTags(
					ComposeEmojiList(
						reactions,
						all
							? CollectAvailableReactions(
								&args.controller->session())
							: DefaultSelected()));
			}
			raw->setDisabled(false);
			raw->setFocusFast();
			break;
		case SelectorState::Disabled:
			state->overlay = std::make_unique<Ui::RpWidget>(parent);
			state->overlay->show();
			raw->geometryValue() | rpl::on_next([=](QRect rect) {
				state->overlay->setGeometry(rect);
			}, state->overlay->lifetime());
			state->overlay->paintRequest() | rpl::on_next([=](QRect clip) {
				auto color = st::boxBg->c;
				color.setAlphaF(0.5);
				QPainter(state->overlay.get()).fillRect(
					clip,
					color);
			}, state->overlay->lifetime());
			[[fallthrough]];
		case SelectorState::Hidden:
			if (Ui::InFocusChain(raw)) {
				raw->parentWidget()->setFocus();
			}
			raw->setDisabled(true);
			raw->focusedChanges(
			) | rpl::on_next([=](bool focused) {
				if (focused) {
					raw->parentWidget()->setFocus();
				}
			}, state->focusLifetime);
			break;
		}
	}, raw->lifetime());

	const auto panel = Ui::CreateChild<TabbedPanel>(
		args.outer.get(),
		args.controller,
		object_ptr<TabbedSelector>(
			nullptr,
			args.controller->uiShow(),
			Window::GifPauseReason::Layer,
			TabbedSelector::Mode::RecentReactions));
	auto panelList = state->unifiedFactoryOwner->unifiedIdsList();
	panel->selector()->provideRecentEmoji(
		ChatHelpers::DocumentListToRecent(panelList));
	panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	panel->hide();
	panel->selector()->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		Data::InsertCustomEmoji(raw, data.document);
	}, panel->lifetime());

	const auto updateEmojiPanelGeometry = [=] {
		const auto parent = panel->parentWidget();
		const auto global = toggle->mapToGlobal({ 0, 0 });
		const auto local = parent->mapFromGlobal(global);
		panel->moveBottomRight(
			local.y(),
			local.x() + toggle->width() * 3);
	};
	const auto scheduleUpdateEmojiPanelGeometry = [=] {
		// updateEmojiPanelGeometry uses not only container geometry, but
		// also container children geometries that will be updated later.
		crl::on_main(raw, updateEmojiPanelGeometry);
	};
	const auto filterCallback = [=](not_null<QEvent*> event) {
		const auto type = event->type();
		if (type == QEvent::Move || type == QEvent::Resize) {
			scheduleUpdateEmojiPanelGeometry();
		}
		return base::EventFilterResult::Continue;
	};
	for (auto widget = (QWidget*)raw
		; widget && widget != args.outer
		; widget = widget->parentWidget()) {
		base::install_event_filter(raw, widget, filterCallback);
	}
	base::install_event_filter(raw, args.outer, filterCallback);
	scheduleUpdateEmojiPanelGeometry();

	toggle->installEventFilter(panel);
	toggle->addClickHandler([=] {
		panel->toggleAnimated();
	});

	raw->geometryValue() | rpl::on_next([=](QRect geometry) {
		toggle->move(
			geometry.x() + geometry.width() - toggle->width(),
			geometry.y() + geometry.height() - toggle->height());
		updateEmojiPanelGeometry();
	}, toggle->lifetime());

	return result;
}

} // namespace

void EditAllowedReactionsBox(
		not_null<Ui::GenericBox*> box,
		EditAllowedReactionsArgs &&args) {
	using namespace Data;
	using namespace rpl::mappers;

	box->setTitle(tr::lng_manage_peer_reactions());
	box->setWidth(st::boxWideWidth);

	enum class Option {
		All,
		Some,
		None,
	};
	using SelectorState = ReactionsSelectorState;
	struct State {
		rpl::variable<Option> option; // For groups.
		rpl::variable<SelectorState> selectorState;
		std::vector<Data::ReactionId> selected;
	};
	const auto allowed = args.allowed;
	const auto optionInitial = (allowed.type != AllowedReactionsType::Some)
		? Option::All
		: allowed.some.empty()
		? Option::None
		: Option::Some;
	const auto state = box->lifetime().make_state<State>(State{
		.option = optionInitial,
	});

	const auto container = box->verticalLayout();
	const auto isGroup = args.isGroup;
	const auto enabled = isGroup
		? nullptr
		: container->add(object_ptr<Ui::SettingsButton>(
			container.get(),
			tr::lng_manage_peer_reactions_enable(),
			st::manageGroupNoIconButton.button));
	if (enabled) {
		enabled->toggleOn(rpl::single(optionInitial != Option::None));
		enabled->toggledValue(
		) | rpl::on_next([=](bool value) {
			state->selectorState = value
				? SelectorState::Active
				: SelectorState::Disabled;
		}, enabled->lifetime());
	}
	const auto group = std::make_shared<Ui::RadioenumGroup<Option>>(
		state->option.current());
	group->setChangedCallback([=](Option value) {
		state->option = value;
	});
	const auto addOption = [&](Option option, const QString &text) {
		if (!isGroup) {
			return;
		}
		container->add(
			object_ptr<Ui::Radioenum<Option>>(
				container,
				group,
				option,
				text,
				st::settingsSendType),
			st::settingsSendTypePadding);
	};
	addOption(Option::All, tr::lng_manage_peer_reactions_all(tr::now));
	addOption(Option::Some, tr::lng_manage_peer_reactions_some(tr::now));
	addOption(Option::None, tr::lng_manage_peer_reactions_none(tr::now));

	const auto about = [](Option option) {
		switch (option) {
		case Option::All: return tr::lng_manage_peer_reactions_all_about();
		case Option::Some: return tr::lng_manage_peer_reactions_some_about();
		case Option::None: return tr::lng_manage_peer_reactions_none_about();
		}
		Unexpected("Option value in EditAllowedReactionsBox.");
	};
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		(isGroup
			? (state->option.value()
				| rpl::map(about)
				| rpl::flatten_latest())
			: tr::lng_manage_peer_reactions_about_channel()));

	const auto wrap = enabled ? nullptr : container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	if (wrap) {
		wrap->toggleOn(state->option.value(
		) | rpl::map(_1 == Option::Some) | rpl::before_next([=](bool some) {
			if (!some) {
				state->selectorState = SelectorState::Hidden;
			}
		}) | rpl::after_next([=](bool some) {
			if (some) {
				state->selectorState = SelectorState::Active;
			}
		}));
		wrap->finishAnimating();
	}
	const auto reactions = wrap ? wrap->entity() : container.get();

	Ui::AddSkip(reactions);

	const auto all = args.list;
	auto selected = (allowed.type != AllowedReactionsType::Some)
		? std::vector<Data::ReactionId>()
		: allowed.some;
	// LoogriGram: custom reactions set elsewhere are not offered back.
	selected.erase(
		ranges::remove_if(selected, &Data::ReactionId::custom),
		end(selected));
	const auto changed = [=](std::vector<Data::ReactionId> chosen) {
		state->selected = std::move(chosen);
	};
	changed(!selected.empty()
		? std::move(selected)
		: !isGroup
		? CollectAvailableReactions(
			&args.navigation->parentController()->session())
		: DefaultSelected());
	Ui::AddSubsectionTitle(
		reactions,
		enabled
			? tr::lng_manage_peer_reactions_available()
			: tr::lng_manage_peer_reactions_some_title(),
		st::manageGroupReactionsFieldPadding);
	reactions->add(AddReactionsSelector(reactions, {
		.outer = box->getDelegate()->outerContainer(),
		.controller = args.navigation->parentController(),
		.title = tr::lng_manage_peer_reactions_available_ph(),
		.list = all,
		.selected = state->selected,
		.callback = changed,
		.stateValue = state->selectorState.value(),
		.all = !args.isGroup,
	}), st::boxRowPadding);

	box->setFocusCallback([=] {
		if (state->option.current() == Option::Some) {
			state->selectorState.force_assign(SelectorState::Active);
		}
	});

	const auto reactionsLimit = container->lifetime().make_state<int>(0);
	if (!isGroup) {
		// LoogriGram: a divider sat here - "create your own emoji packs"
		// and the boost level the chosen custom reactions needed.
		Ui::AddSkip(container, st::manageGroupReactionsTextSkip);
		Ui::AddDivider(container);

		const auto session = &args.navigation->parentController()->session();

		const auto wrap = container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				container,
				object_ptr<Ui::VerticalLayout>(container)));
		const auto max = Data::UniqueReactionsLimit(session->user());
		const auto inactiveColor = std::make_optional(st::windowSubTextFg->c);
		const auto activeColor = std::make_optional(
			st::windowActiveTextFg->c);
		const auto inner = wrap->entity();
		Ui::AddSkip(inner);
		Ui::AddSubsectionTitle(
			inner,
			tr::lng_manage_peer_reactions_max_title(),
			st::manageGroupReactionsMaxSubtitlePadding);
		Ui::AddSkip(inner);
		const auto line = inner->add(
			object_ptr<Ui::RpWidget>(inner),
			st::boxRowPadding);
		Ui::AddSkip(inner);
		Ui::AddSkip(inner);
		const auto left = Ui::CreateChild<Ui::FlatLabel>(
			line,
			QString::number(1),
			st::defaultFlatLabel);
		const auto center = Ui::CreateChild<Ui::FlatLabel>(
			line,
			st::defaultFlatLabel);
		const auto right = Ui::CreateChild<Ui::FlatLabel>(
			line,
			QString::number(max),
			st::defaultFlatLabel);
		const auto slider = Ui::CreateChild<Ui::MediaSlider>(
			line,
			st::settingsScale);
		rpl::combine(
			line->sizeValue(),
			left->sizeValue(),
			center->sizeValue(),
			right->sizeValue()
		) | rpl::on_next([=](
				const QSize &s,
				const QSize &leftSize,
				const QSize &centerSize,
				const QSize &rightSize) {
			const auto sliderHeight = st::settingsScale.seekSize.height();
			line->resize(
				line->width(),
				leftSize.height() + sliderHeight * 2);
			{
				const auto r = line->rect();
				slider->setGeometry(
					0,
					r.height() - sliderHeight * 1.5,
					r.width(),
					sliderHeight);
			}
			left->moveToLeft(0, 0);
			right->moveToRight(0, 0);
			center->moveToLeft((s.width() - centerSize.width()) / 2, 0);
		}, line->lifetime());

		const auto updateLabels = [=](int limit) {
			left->setTextColorOverride((limit <= 1)
				? activeColor
				: inactiveColor);

			center->setText(tr::lng_manage_peer_reactions_max_slider(
				tr::now,
				lt_count,
				limit));
			center->setTextColorOverride(activeColor);

			right->setTextColorOverride((limit >= max)
				? activeColor
				: inactiveColor);

			(*reactionsLimit) = limit;
		};
		const auto current = args.allowed.maxCount
			? std::clamp(1, args.allowed.maxCount, max)
			: max / 2;
		slider->setPseudoDiscrete(
			max,
			[=](int index) { return index + 1; },
			current,
			updateLabels,
			updateLabels);
		updateLabels(current);

		wrap->toggleOn(rpl::single(
			optionInitial != Option::None
		) | rpl::then(
			state->selectorState.value(
			) | rpl::map(rpl::mappers::_1 == SelectorState::Active)));

		Ui::AddDividerText(inner, tr::lng_manage_peer_reactions_max_about());

		// LoogriGram: no "paid reactions" toggle when managing a channel. The
		// flag it edited is dropped in Data::Parse regardless, so the switch
		// could only ever have lied about what it does.
	}
	const auto collect = [=] {
		auto result = AllowedReactions();
		result.maxCount = (*reactionsLimit);
		if (isGroup
			? (state->option.current() == Option::Some)
			: (enabled->toggled())) {
			result.some = state->selected;
		}
		auto some = result.some;
		auto simple = all | ranges::views::transform(
			&Data::Reaction::id
		) | ranges::to_vector;
		ranges::sort(some);
		ranges::sort(simple);
		result.type = isGroup
			? (state->option.current() != Option::All
				? AllowedReactionsType::Some
				: AllowedReactionsType::All)
			: (some == simple)
			? AllowedReactionsType::Default
			: AllowedReactionsType::Some;
		return result;
	};

	box->addButton(tr::lng_settings_save(), [=] {
		const auto result = collect();
		box->closeBox();
		args.save(result);
	});
	box->addButton(tr::lng_cancel(), [=] {
		box->closeBox();
	});
}

void SaveAllowedReactions(
		not_null<PeerData*> peer,
		const Data::AllowedReactions &allowed) {
	auto ids = allowed.some | ranges::views::transform(
		Data::ReactionToMTP
	) | ranges::to<QVector<MTPReaction>>;

	using Flag = MTPmessages_SetChatAvailableReactions::Flag;
	using Type = Data::AllowedReactionsType;
	const auto updated = (allowed.type != Type::Some)
		? MTP_chatReactionsAll(MTP_flags((allowed.type == Type::Default)
			? MTPDchatReactionsAll::Flag(0)
			: MTPDchatReactionsAll::Flag::f_allow_custom))
		: allowed.some.empty()
		? MTP_chatReactionsNone()
		: MTP_chatReactionsSome(MTP_vector<MTPReaction>(ids));
	// LoogriGram: a channel owner could allow paid reactions on their posts.
	// Paid reactions are deleted, so the flag is never set.
	const auto maxCount = allowed.maxCount;
	peer->session().api().request(MTPmessages_SetChatAvailableReactions(
		MTP_flags(Flag()
			| (maxCount ? Flag::f_reactions_limit : Flag())),
		peer->input(),
		updated,
		MTP_int(maxCount),
		MTP_bool(false)
	)).done([=](const MTPUpdates &result) {
		peer->session().api().applyUpdates(result);
		auto parsed = Data::Parse(updated, maxCount);
		if (const auto chat = peer->asChat()) {
			chat->setAllowedReactions(parsed);
		} else if (const auto channel = peer->asChannel()) {
			channel->setAllowedReactions(parsed);
		} else {
			Unexpected("Invalid peer type in SaveAllowedReactions.");
		}
	}).fail([=](const MTP::Error &error) {
		if (error.type() == u"REACTION_INVALID"_q) {
			peer->updateFullForced();
			peer->owner().reactions().refreshDefault();
		}
	}).send();
}
