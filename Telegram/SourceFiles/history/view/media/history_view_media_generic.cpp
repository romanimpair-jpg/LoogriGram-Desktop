/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_media_generic.h"

#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_cursor_state.h"
#include "ui/chat/chat_style.h"
#include "ui/effects/ripple_animation.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rect.h"
#include "ui/round_rect.h"
#include "ui/userpic_view.h"
#include "styles/style_chat.h"
#include "styles/style_polls.h"

namespace HistoryView {
namespace {

constexpr auto kAdditionalPrizesWithLineOpacity = 0.6;

class ButtonPart final : public MediaGenericPart {
public:
	ButtonPart(
		const QString &text,
		QMargins margins,
		Fn<void()> repaint,
		ClickHandlerPtr link,
		QColor bg = QColor(0, 0, 0, 0));

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	Ui::Text::String _text;
	QMargins _margins;
	QColor _bg;
	QSize _size;

	ClickHandlerPtr _link;
	std::unique_ptr<Ui::RippleAnimation> _ripple;
	Fn<void()> _repaint;

	mutable QPoint _lastPoint;

};

ButtonPart::ButtonPart(
	const QString &text,
	QMargins margins,
	Fn<void()> repaint,
	ClickHandlerPtr link,
	QColor bg)
: _text(st::semiboldTextStyle, text)
, _margins(margins)
, _bg(bg)
, _size(
	(_text.maxWidth()
		+ st::msgServiceGiftBoxButtonHeight
		+ st::msgServiceGiftBoxButtonPadding.left()
		+ st::msgServiceGiftBoxButtonPadding.right()),
	st::msgServiceGiftBoxButtonHeight)
, _link(std::move(link))
, _repaint(std::move(repaint)) {
}

void ButtonPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	PainterHighQualityEnabler hq(p);

	const auto customColors = (_bg.alpha() > 0);

	const auto position = QPoint(
		(outerWidth - width()) / 2 + _margins.left(),
		_margins.top());
	p.translate(position);

	p.setPen(Qt::NoPen);
	p.setBrush(customColors ? QBrush(_bg) : context.st->msgServiceBg());
	const auto radius = _size.height() / 2.;
	const auto r = Rect(_size);
	p.drawRoundedRect(r, radius, radius);

	auto white = QColor(255, 255, 255);
	const auto fg = customColors ? white : context.st->msgServiceFg()->c;

	// LoogriGram: upstream sparkled this pill with the colored mini stars it
	// came with from the unique gift view. It is a "View community" button
	// now, so it is drawn plain.
	if (_ripple) {
		const auto opacity = p.opacity();
		const auto ripple = customColors
			? anim::with_alpha(fg, .3)
			: context.messageStyle()->msgWaveformInactive->c;
		p.setOpacity(st::historyPollRippleOpacity);
		_ripple->paint(
			p,
			0,
			0,
			width(),
			&ripple);
		p.setOpacity(opacity);
	}

	p.setPen(fg);
	_text.draw(
		p,
		0,
		(_size.height() - _text.minHeight()) / 2,
		_size.width(),
		style::al_top);

	p.translate(-position);
}

TextState ButtonPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	point -= QPoint{
		(outerWidth - width()) / 2 + _margins.left(),
		_margins.top()
	};
	if (QRect(QPoint(), _size).contains(point)) {
		auto result = TextState();
		result.link = _link;
		_lastPoint = point;
		return result;
	}
	return {};
}

void ButtonPart::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	if (p != _link) {
		return;
	} else if (pressed) {
		if (!_ripple) {
			const auto radius = _size.height() / 2;
			_ripple = std::make_unique<Ui::RippleAnimation>(
				st::defaultRippleAnimation,
				Ui::RippleAnimation::RoundRectMask(_size, radius),
				_repaint);
		}
		_ripple->add(_lastPoint);
	} else if (_ripple) {
		_ripple->lastStop();
	}
}

QSize ButtonPart::countOptimalSize() {
	return {
		_margins.left() + _size.width() + _margins.right(),
		_margins.top() + _size.height() + _margins.bottom(),
	};
}

QSize ButtonPart::countCurrentSize(int newWidth) {
	return optimalSize();
}

} // namespace

TextState MediaGenericPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	return {};
}

void MediaGenericPart::clickHandlerPressedChanged(
	const ClickHandlerPtr &p,
	bool pressed) {
}

bool MediaGenericPart::hasHeavyPart() {
	return false;
}

void MediaGenericPart::unloadHeavyPart() {
}

auto MediaGenericPart::stickerTakePlayer(
	not_null<DocumentData*> data,
	const Lottie::ColorReplacements *replacements
) -> std::unique_ptr<StickerPlayer> {
	return nullptr;
}

uint16 MediaGenericPart::fullSelectionLength() const {
	return 0;
}

TextSelection MediaGenericPart::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return selection;
}

TextForMimeData MediaGenericPart::selectedText(
		TextSelection selection) const {
	return {};
}

MediaGeneric::MediaGeneric(
	not_null<Element*> parent,
	Fn<void(
		not_null<MediaGeneric*>,
		Fn<void(std::unique_ptr<Part>)>)> generate,
	MediaGenericDescriptor &&descriptor)
: Media(parent)
, _paintBgFactory(std::move(descriptor.paintBgFactory))
, _paintBg(_paintBgFactory ? _paintBgFactory() : nullptr)
, _fullAreaLink(descriptor.fullAreaLink)
, _maxWidthCap(descriptor.maxWidth)
, _minWidth(descriptor.minWidth)
, _expandCurrentWidth(descriptor.expandCurrentWidth)
, _fitToContent(descriptor.fitToContent)
, _service(descriptor.service)
, _hideServiceText(descriptor.hideServiceText) {
	generate(this, [&](std::unique_ptr<Part> part) {
		_entries.push_back({
			.object = std::move(part),
		});
	});
}

MediaGeneric::~MediaGeneric() {
	if (hasHeavyPart()) {
		unloadHeavyPart();
		_parent->checkHeavyPart();
	}
}

QSize MediaGeneric::countOptimalSize() {
	const auto cap = _maxWidthCap
		? _maxWidthCap
		: st::chatGiveawayWidth;

	auto contentWidth = 0;
	for (auto &entry : _entries) {
		const auto raw = entry.object.get();
		raw->initDimensions();
		accumulate_max(contentWidth, raw->maxWidth());
	}
	const auto maxWidth = (_fitToContent && contentWidth)
		? std::clamp(contentWidth, std::min(_minWidth, cap), cap)
		: cap;

	auto top = 0;
	for (auto &entry : _entries) {
		top += entry.object->resizeGetHeight(maxWidth);
	}
	return { maxWidth, top };
}

QSize MediaGeneric::countCurrentSize(int newWidth) {
	if (!_expandCurrentWidth && newWidth > maxWidth()) {
		newWidth = maxWidth();
	}
	auto top = 0;
	for (auto &entry : _entries) {
		top += entry.object->resizeGetHeight(newWidth);
	}
	return { newWidth, top };
}

void MediaGeneric::draw(Painter &p, const PaintContext &context) const {
	const auto outer = width();
	if (outer < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return;
	}
	if (!_paintBg && _paintBgFactory) {
		_paintBg = _paintBgFactory();
	}
	if (_paintBg) {
		_paintBg(p, context, this);
	} else if (_service) {
		PainterHighQualityEnabler hq(p);
		const auto radius = st::msgServiceGiftBoxRadius;
		p.setPen(Qt::NoPen);
		p.setBrush(context.st->msgServiceBg());
		const auto rect = QRect(0, 0, width(), height());
		if (parent()->data()->inlineReplyKeyboard()) {
			const auto half = rect.height() / 2;
			p.setClipRect(rect - QMargins(0, 0, 0, half));
			p.drawRoundedRect(rect, radius, radius);
			p.setClipRect(rect - QMargins(0, rect.height() - half, 0, 0));
			const auto small = Ui::BubbleRadiusSmall();
			p.drawRoundedRect(rect, small, small);
			p.setClipping(false);
		} else {
			p.drawRoundedRect(rect, radius, radius);
		}
	}

	const auto fullSelection = context.selected();
	auto translated = 0;
	auto symbolOffset = uint16(0);
	for (const auto &entry : _entries) {
		const auto raw = entry.object.get();
		const auto height = raw->height();
		const auto length = raw->fullSelectionLength();
		if (length > 0 && !fullSelection) {
			const auto local = UnshiftItemSelection(
				context.selection,
				symbolOffset);
			raw->draw(p, this, context.withSelection(local), outer);
		} else {
			raw->draw(p, this, context, outer);
		}
		translated += height;
		symbolOffset = uint16(symbolOffset + length);
		p.translate(0, height);
	}
	p.translate(0, -translated);
}

TextState MediaGeneric::textState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(_parent);

	const auto outer = width();
	if (outer < st::msgPadding.left() + st::msgPadding.right() + 1) {
		return result;
	}

	if (_fullAreaLink && QRect(0, 0, width(), height()).contains(point)) {
		result.link = _fullAreaLink;
		return result;
	}

	auto symbolOffset = uint16(0);
	for (const auto &entry : _entries) {
		const auto raw = entry.object.get();
		const auto height = raw->height();
		const auto length = raw->fullSelectionLength();
		if (point.y() >= 0 && point.y() < height) {
			const auto part = raw->textState(point, request, outer);
			result.link = part.link;
			result.cursor = part.cursor;
			if (length > 0) {
				result.symbol = uint16(symbolOffset + part.symbol);
				result.afterSymbol = part.afterSymbol;
				result.overMessageText
					= (part.cursor == CursorState::Text);
			} else {
				result.symbol = symbolOffset;
			}
			return result;
		}
		point.setY(point.y() - height);
		symbolOffset = uint16(symbolOffset + length);
	}
	result.symbol = symbolOffset;
	return result;
}

void MediaGeneric::clickHandlerActiveChanged(
		const ClickHandlerPtr &p,
		bool active) {
}

void MediaGeneric::clickHandlerPressedChanged(
		const ClickHandlerPtr &p,
		bool pressed) {
	for (const auto &entry : _entries) {
		entry.object->clickHandlerPressedChanged(p, pressed);
	}
}

bool MediaGeneric::hasTextForCopy() const {
	return fullSelectionLength() > 0;
}

uint16 MediaGeneric::fullSelectionLength() const {
	auto total = uint16(0);
	for (const auto &entry : _entries) {
		total = uint16(total + entry.object->fullSelectionLength());
	}
	return total;
}

TextForMimeData MediaGeneric::selectedText(TextSelection selection) const {
	auto offset = uint16(0);
	auto result = TextForMimeData();
	for (const auto &entry : _entries) {
		const auto length = entry.object->fullSelectionLength();
		if (length > 0) {
			auto part = entry.object->selectedText(
				UnshiftItemSelection(selection, offset));
			if (!part.empty()) {
				if (result.empty()) {
					result = std::move(part);
				} else {
					result.append('\n').append(std::move(part));
				}
			}
		}
		offset = uint16(offset + length);
	}
	return result;
}

TextSelection MediaGeneric::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	if (selection == FullSelection) {
		return selection;
	}
	auto offset = uint16(0);
	auto firstFrom = std::optional<uint16>();
	auto firstOffset = uint16(0);
	auto lastTo = uint16(0);
	auto lastOffset = uint16(0);
	for (const auto &entry : _entries) {
		const auto length = entry.object->fullSelectionLength();
		if (length > 0) {
			const auto end = uint16(offset + length);
			if (selection.from < end && selection.to > offset) {
				const auto from = uint16((selection.from > offset)
					? (selection.from - offset)
					: 0);
				const auto to = uint16((selection.to < end)
					? (selection.to - offset)
					: length);
				const auto local = entry.object->adjustSelection(
					{ from, to },
					type);
				if (!firstFrom.has_value()) {
					firstFrom = local.from;
					firstOffset = offset;
				}
				lastTo = local.to;
				lastOffset = offset;
			}
		}
		offset = uint16(offset + length);
	}
	if (!firstFrom.has_value()) {
		return selection;
	}
	return {
		uint16(firstOffset + *firstFrom),
		uint16(lastOffset + lastTo),
	};
}

std::unique_ptr<StickerPlayer> MediaGeneric::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	for (const auto &entry : _entries) {
		if (auto result = entry.object->stickerTakePlayer(
				data,
				replacements)) {
			return result;
		}
	}
	return nullptr;
}

bool MediaGeneric::hideFromName() const {
	return !parent()->data()->Has<HistoryMessageForwarded>();
}

bool MediaGeneric::hideServiceText() const {
	return _hideServiceText;
}

bool MediaGeneric::hasHeavyPart() const {
	for (const auto &entry : _entries) {
		if (entry.object->hasHeavyPart()) {
			return true;
		}
	}
	return false;
}

void MediaGeneric::unloadHeavyPart() {
	_paintBg = nullptr;
	for (const auto &entry : _entries) {
		entry.object->unloadHeavyPart();
	}
}

QMargins MediaGeneric::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom()
		? st::msgPadding.top()
		: st::mediaInBubbleSkip;
	auto tshift = isBubbleTop()
		? st::msgPadding.bottom()
		: st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

MediaGenericTextPart::MediaGenericTextPart(
	TextWithEntities text,
	QMargins margins,
	const style::TextStyle &st,
	const base::flat_map<uint16, ClickHandlerPtr> &links,
	const Ui::Text::MarkedContext &context,
	style::align align)
: _text(st::msgMinWidth)
, _margins(margins)
, _align(align) {
	_text.setMarkedText(
		st,
		text,
		kMarkupTextOptions,
		context);
	for (const auto &[index, link] : links) {
		_text.setLink(index, link);
	}
}

void MediaGenericTextPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto use = (width() - _margins.left() - _margins.right());
	setupPen(p, owner, context);
	_text.draw(p, {
		.position = {
			((_align == style::al_top)
				? ((outerWidth - use) / 2)
				: _margins.left()),
			_margins.top(),
		},
		.outerWidth = outerWidth,
		.availableWidth = use,
		.align = _align,
		.palette = &(owner->service()
			? context.st->serviceTextPalette()
			: context.messageStyle()->textPalette),
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
		.selection = context.selection,
		.elisionLines = elisionLines(),
	});
}

void MediaGenericTextPart::setupPen(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const {
	const auto service = owner->service();
	p.setPen(service
		? context.st->msgServiceFg()
		: context.messageStyle()->historyTextFg);
}

int MediaGenericTextPart::elisionLines() const {
	return 0;
}

TextState MediaGenericTextPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	const auto use = (width() - _margins.left() - _margins.right());
	point -= QPoint{
		((_align == style::al_top)
			? ((outerWidth - use) / 2)
			: _margins.left()),
		_margins.top(),
	};
	auto forText = request.forText();
	forText.align = _align;
	return TextState(nullptr, _text.getState(point, use, forText));
}

uint16 MediaGenericTextPart::fullSelectionLength() const {
	return _text.length();
}

TextSelection MediaGenericTextPart::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return _text.adjustSelection(selection, type);
}

TextForMimeData MediaGenericTextPart::selectedText(
		TextSelection selection) const {
	return _text.toTextForMimeData(selection);
}

QSize MediaGenericTextPart::countOptimalSize() {
	const auto lines = elisionLines();
	const auto height = lines
		? std::min(_text.minHeight(), lines * _text.style()->font->height)
		: _text.minHeight();
	return {
		_margins.left() + _text.maxWidth() + _margins.right(),
		_margins.top() + height + _margins.bottom(),
	};
}

QSize MediaGenericTextPart::countCurrentSize(int newWidth) {
	auto skip = _margins.left() + _margins.right();
	const auto size = (_align == style::al_top)
		? Ui::Text::CountOptimalTextSize(
			_text,
			st::msgMinWidth,
			std::max(st::msgMinWidth, newWidth - skip))
		: QSize(newWidth - skip, _text.countHeight(newWidth - skip));
	const auto lines = elisionLines();
	const auto height = lines
		? std::min(size.height(), lines * _text.style()->font->height)
		: size.height();
	return {
		size.width() + skip,
		_margins.top() + height + _margins.bottom(),
	};
}

LambdaGenericPart::LambdaGenericPart(
	QSize size,
	Fn<void(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth)> draw)
: _size(size)
, _draw(std::move(draw)) {
}

void LambdaGenericPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	if (_draw) {
		_draw(p, owner, context, outerWidth);
	}
}

QSize LambdaGenericPart::countOptimalSize() {
	return _size;
}

QSize LambdaGenericPart::countCurrentSize(int newWidth) {
	return { newWidth, _size.height() };
}

StickerInBubblePart::StickerInBubblePart(
	not_null<Element*> parent,
	Element *replacing,
	Fn<Data()> lookup,
	QMargins padding)
: _parent(parent)
, _lookup(std::move(lookup))
, _padding(padding) {
	ensureCreated(replacing);
}

void StickerInBubblePart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	ensureCreated();
	if (_sticker) {
		const auto stickerSize = _sticker->countOptimalSize();
		const auto sticker = QRect(
			(outerWidth - stickerSize.width()) / 2,
			_padding.top() + _skipTop,
			stickerSize.width(),
			stickerSize.height());
		_sticker->draw(p, context, sticker);
	}
}

TextState StickerInBubblePart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	auto result = TextState(_parent);
	if (_sticker) {
		const auto stickerSize = _sticker->countOptimalSize();
		const auto sticker = QRect(
			(outerWidth - stickerSize.width()) / 2,
			_padding.top() + _skipTop,
			stickerSize.width(),
			stickerSize.height());
		if (sticker.contains(point)) {
			result.link = _link;
		}
	}
	return result;
}

bool StickerInBubblePart::hasHeavyPart() {
	return _sticker && _sticker->hasHeavyPart();
}

void StickerInBubblePart::unloadHeavyPart() {
	if (_sticker) {
		_sticker->unloadHeavyPart();
	}
}

std::unique_ptr<StickerPlayer> StickerInBubblePart::stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements) {
	return _sticker
		? _sticker->stickerTakePlayer(data, replacements)
		: nullptr;
}

QSize StickerInBubblePart::countOptimalSize() {
	ensureCreated();
	const auto size = _sticker ? _sticker->countOptimalSize() : [&] {
		const auto fallback = _lookup().size;
		return QSize{ fallback, fallback };
	}();
	return {
		_padding.left() + size.width() + _padding.right(),
		_padding.top() + size.height() + _padding.bottom(),
	};
}

QSize StickerInBubblePart::countCurrentSize(int newWidth) {
	return { newWidth, minHeight() };
}

void StickerInBubblePart::ensureCreated(Element *replacing) const {
	if (_sticker) {
		return;
	} else if (const auto data = _lookup()) {
		const auto sticker = data.sticker;
		if (sticker->sticker()) {
			const auto skipPremiumEffect = true;
			_link = data.link;
			_skipTop = data.skipTop;
			_sticker.emplace(_parent, sticker, skipPremiumEffect, replacing);
			if (data.stopOnLastFrame) {
				_sticker->setStopOnLastFrame(true);
			}
			_sticker->initSize(data.size);
			_sticker->setCustomCachingTag(data.cacheTag);
		}
	}
}

DynamicImagePart::DynamicImagePart(
	not_null<Element*> parent,
	std::shared_ptr<Ui::DynamicImage> image,
	int size,
	QMargins margins,
	ClickHandlerPtr link,
	bool communityEffect)
: _parent(parent)
, _image(std::move(image))
, _link(std::move(link))
, _margins(margins)
, _size(size)
, _communityEffect(communityEffect) {
}

DynamicImagePart::~DynamicImagePart() = default;

void DynamicImagePart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	if (!_subscribed) {
		_subscribed = true;
		const auto raw = _parent;
		_image->subscribeToUpdates([raw] { raw->repaint(); });
		raw->history()->owner().registerHeavyViewPart(raw);
	}
	const auto left = (outerWidth - _size) / 2;
	const auto top = _margins.top();
	if (_communityEffect) {
		if (!_communityCache) {
			_communityCache = std::make_unique<Ui::CommunityUserpicEffect>();
		}
		Ui::PaintCommunityUserpicEffect(
			p,
			*_communityCache,
			left,
			top,
			_size,
			context.st->msgServiceBg()->c);
	}
	p.drawImage(QPoint(left, top), _image->image(_size));
}

TextState DynamicImagePart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	auto result = TextState(_parent);
	const auto left = (outerWidth - _size) / 2;
	if (_link && QRect(left, _margins.top(), _size, _size).contains(point)) {
		result.link = _link;
	}
	return result;
}

bool DynamicImagePart::hasHeavyPart() {
	return _subscribed;
}

void DynamicImagePart::unloadHeavyPart() {
	if (_subscribed) {
		_subscribed = false;
		_image->subscribeToUpdates(nullptr);
	}
	_communityCache = nullptr;
}

QSize DynamicImagePart::countOptimalSize() {
	return {
		_margins.left() + _size + _margins.right(),
		_margins.top() + _size + _margins.bottom(),
	};
}

QSize DynamicImagePart::countCurrentSize(int newWidth) {
	return { newWidth, minHeight() };
}

std::unique_ptr<MediaGenericPart> MakeGenericButtonPart(
		const QString &text,
		QMargins margins,
		Fn<void()> repaint,
		ClickHandlerPtr link,
		QColor bg) {
	return std::make_unique<ButtonPart>(text, margins, repaint, link, bg);
}

TextPartColored::TextPartColored(
	TextWithEntities text,
	QMargins margins,
	Fn<QColor(const PaintContext &)> color,
	const style::TextStyle &st,
	const base::flat_map<uint16, ClickHandlerPtr> &links,
	const Ui::Text::MarkedContext &context)
: MediaGenericTextPart(text, margins, st, links, context)
, _color(std::move(color)) {
}

void TextPartColored::setupPen(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const {
	p.setPen(_color(context));
}

AttributeTable::AttributeTable(
	std::vector<Entry> entries,
	QMargins margins,
	Fn<QColor(const PaintContext &)> labelColor,
	Fn<QColor(const PaintContext &)> valueColor,
	const Ui::Text::MarkedContext &context)
: _margins(margins)
, _labelColor(std::move(labelColor))
, _valueColor(std::move(valueColor)) {
	for (const auto &entry : entries) {
		_parts.emplace_back();
		auto &part = _parts.back();
		part.label.setText(st::chatUniqueTextStyle, entry.label);
		part.value.setMarkedText(
			st::chatUniqueTextStyle,
			entry.value,
			kMarkupTextOptions,
			context);
	}
}

void AttributeTable::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	const auto labelRight = _valueLeft - st::chatUniqueTableSkip;
	const auto palette = &context.st->serviceTextPalette();
	auto top = _margins.top();
	const auto paint = [&](
			const Ui::Text::String &text,
			int left,
			int availableWidth,
			style::align align) {
		text.draw(p, {
			.position = { left, top },
			.outerWidth = outerWidth,
			.availableWidth = availableWidth,
			.align = align,
			.palette = palette,
			.spoiler = Ui::Text::DefaultSpoilerCache(),
			.now = context.now,
			.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
			.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
			.elisionLines = 1,
		});
	};
	const auto forLabel = labelRight - _margins.left();
	const auto forValue = width() - _valueLeft - _margins.right();
	for (const auto &part : _parts) {
		p.setPen(_labelColor(context));
		paint(part.label, _margins.left(), forLabel, style::al_topright);
		p.setPen(_valueColor(context));
		paint(part.value, _valueLeft, forValue, style::al_topleft);
		top += st::normalFont->height + st::chatUniqueRowSkip;
	}
}

TextState AttributeTable::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	auto top = _margins.top();
	for (const auto &part : _parts) {
		const auto height = st::normalFont->height + st::chatUniqueRowSkip;
		if (point.y() >= top && point.y() < top + height) {
			point -= QPoint((outerWidth - width()) / 2 + _valueLeft, top);
			auto result = TextState();
			auto forText = request.forText();
			forText.align = style::al_topleft;
			result.link = part.value.getState(point, width(), forText).link;
			return result;
		}
		top += height;
	}
	return {};
}

QSize AttributeTable::countOptimalSize() {
	auto maxLabel = 0;
	auto maxValue = 0;
	for (const auto &part : _parts) {
		maxLabel = std::max(maxLabel, part.label.maxWidth());
		maxValue = std::max(maxValue, part.value.maxWidth());
	}
	const auto skip = st::chatUniqueTableSkip;
	const auto row = st::normalFont->height + st::chatUniqueRowSkip;
	const auto height = int(_parts.size()) * row - st::chatUniqueRowSkip;
	return {
		_margins.left() + maxLabel + skip + maxValue + _margins.right(),
		_margins.top() + height + _margins.bottom(),
	};
}

QSize AttributeTable::countCurrentSize(int newWidth) {
	const auto skip = st::chatUniqueTableSkip;
	const auto width = newWidth - _margins.left() - _margins.right() - skip;
	auto maxLabel = 0;
	auto maxValue = 0;
	for (const auto &part : _parts) {
		maxLabel = std::max(maxLabel, part.label.maxWidth());
		maxValue = std::max(maxValue, part.value.maxWidth());
	}
	if (width <= 0 || !maxLabel) {
		_valueLeft = _margins.left();
	} else if (!maxValue) {
		_valueLeft = newWidth - _margins.right();
	} else {
		_valueLeft = _margins.left()
			+ int((int64(maxLabel) * width) / (maxLabel + maxValue))
			+ skip;
	}
	return { newWidth, minHeight() };
}
} // namespace HistoryView
