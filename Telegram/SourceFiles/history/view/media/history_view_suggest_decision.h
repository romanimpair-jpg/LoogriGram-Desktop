/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct HistoryMessageSuggestion;
struct HistoryServiceSuggestDecision;

namespace HistoryView {

class Element;
class MediaGeneric;
class MediaGenericPart;

// LoogriGram: the cards for a free suggested post and for its answer. The
// money parts of both - the price row, what each side pays or is paid, the
// refund terms and the low-balance warning - are not here: a suggestion or
// an answer that carries a price is hidden before it is parsed.
auto GenerateSuggestDecisionMedia(
	not_null<Element*> parent,
	not_null<const HistoryServiceSuggestDecision*> decision
) -> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

auto GenerateSuggestRequestMedia(
	not_null<Element*> parent,
	not_null<const HistoryMessageSuggestion*> suggest
) -> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

} // namespace HistoryView
