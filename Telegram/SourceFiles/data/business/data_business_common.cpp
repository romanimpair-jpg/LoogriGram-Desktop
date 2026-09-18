/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/business/data_business_common.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_user.h"

namespace Data {
namespace {

constexpr auto kDay = WorkingInterval::kDay;
constexpr auto kWeek = WorkingInterval::kWeek;
constexpr auto kInNextDayMax = WorkingInterval::kInNextDayMax;

[[nodiscard]] WorkingIntervals SortAndMerge(WorkingIntervals intervals) {
	auto &list = intervals.list;
	ranges::sort(list, ranges::less(), &WorkingInterval::start);
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		if (i && list[i] && list[i -1] && list[i].start <= list[i - 1].end) {
			list[i - 1] = list[i - 1].united(list[i]);
			list[i] = {};
		}
		if (!list[i]) {
			list.erase(list.begin() + i);
			--i;
			--count;
		}
	}
	return intervals;
}

[[nodiscard]] WorkingIntervals MoveTailToFront(WorkingIntervals intervals) {
	auto &list = intervals.list;
	auto after = WorkingInterval{ kWeek, kWeek + kDay };
	while (!list.empty()) {
		if (const auto tail = list.back().intersected(after)) {
			list.back().end = tail.start;
			if (!list.back()) {
				list.pop_back();
			}
			list.insert(begin(list), tail.shifted(-kWeek));
		} else {
			break;
		}
	}
	return intervals;
}

} // namespace

BusinessDetails FromMTP(
		not_null<Session*> owner,
		const tl::conditional<MTPBusinessWorkHours> &hours,
		const tl::conditional<MTPBusinessLocation> &location,
		const tl::conditional<MTPBusinessIntro> &intro) {
	auto result = BusinessDetails();
	if (hours) {
		const auto &data = hours->data();
		result.hours.timezoneId = qs(data.vtimezone_id());
		result.hours.intervals.list = ranges::views::all(
			data.vweekly_open().v
		) | ranges::views::transform([](const MTPBusinessWeeklyOpen &open) {
			const auto &data = open.data();
			return WorkingInterval{
				data.vstart_minute().v * 60,
				data.vend_minute().v * 60,
			};
		}) | ranges::to_vector;
	}
	if (location) {
		const auto &data = location->data();
		result.location.address = qs(data.vaddress());
		if (const auto point = data.vgeo_point()) {
			point->match([&](const MTPDgeoPoint &data) {
				result.location.point = LocationPoint(data);
			}, [&](const MTPDgeoPointEmpty &) {
			});
		}
	}
	if (intro) {
		const auto &data = intro->data();
		result.intro.title = qs(data.vtitle());
		result.intro.description = qs(data.vdescription());
		if (const auto document = data.vsticker()) {
			result.intro.sticker = owner->processDocument(*document);
			if (!result.intro.sticker->sticker()) {
				result.intro.sticker = nullptr;
			}
		}
	}
	return result;
}

WorkingIntervals WorkingIntervals::normalized() const {
	return SortAndMerge(MoveTailToFront(SortAndMerge(*this)));
}

WorkingIntervals ExtractDayIntervals(
		const WorkingIntervals &intervals,
		int dayIndex) {
	Expects(dayIndex >= 0 && dayIndex < 7);

	auto result = WorkingIntervals();
	auto &list = result.list;
	for (const auto &interval : intervals.list) {
		const auto now = interval.intersected(
			{ (dayIndex - 1) * kDay, (dayIndex + 2) * kDay });
		const auto after = interval.intersected(
			{ (dayIndex + 6) * kDay, (dayIndex + 9) * kDay });
		const auto before = interval.intersected(
			{ (dayIndex - 8) * kDay, (dayIndex - 5) * kDay });
		if (now) {
			list.push_back(now.shifted(-dayIndex * kDay));
		}
		if (after) {
			list.push_back(after.shifted(-(dayIndex + 7) * kDay));
		}
		if (before) {
			list.push_back(before.shifted(-(dayIndex - 7) * kDay));
		}
	}
	result = result.normalized();

	const auto outside = [&](WorkingInterval interval) {
		return (interval.end <= 0) || (interval.start >= kDay);
	};
	list.erase(ranges::remove_if(list, outside), end(list));

	if (!list.empty() && list.back().start <= 0 && list.back().end >= kDay) {
		list.back() = { 0, kDay };
	} else if (!list.empty() && (list.back().end > kDay + kInNextDayMax)) {
		list.back() = list.back().intersected({ 0, kDay });
	}
	if (!list.empty() && list.front().start <= 0) {
		if (list.front().start < 0
			&& list.front().end <= kInNextDayMax
			&& list.front().start > -kDay) {
			list.erase(begin(list));
		} else {
			list.front() = list.front().intersected({ 0, kDay });
			if (!list.front()) {
				list.erase(begin(list));
			}
		}
	}

	return result;
}

bool IsFullOpen(const WorkingIntervals &extractedDay) {
	return extractedDay // 00:00-23:59 or 00:00-00:00 (next day)
		&& (extractedDay.list.front() == WorkingInterval{ 0, kDay - 60 }
			|| extractedDay.list.front() == WorkingInterval{ 0, kDay });
}

WorkingIntervals RemoveDayIntervals(
		const WorkingIntervals &intervals,
		int dayIndex) {
	auto result = intervals.normalized();
	auto &list = result.list;
	const auto day = WorkingInterval{ 0, kDay };
	const auto shifted = day.shifted(dayIndex * kDay);
	auto before = WorkingInterval{ 0, shifted.start };
	auto after = WorkingInterval{ shifted.end, kWeek };
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		if (list[i].end <= shifted.start || list[i].start >= shifted.end) {
			continue;
		} else if (list[i].end <= shifted.start + kInNextDayMax
			&& (list[i].start < shifted.start
				|| (!dayIndex // This 'Sunday' finishing on next day <= 6:00.
					&& list[i].start == shifted.start
					&& list.back().end >= kWeek))) {
			continue;
		} else if (const auto first = list[i].intersected(before)) {
			list[i] = first;
			if (const auto second = list[i].intersected(after)) {
				list.push_back(second);
			}
		} else if (const auto second = list[i].intersected(after)) {
			list[i] = second;
		} else {
			list.erase(list.begin() + i);
			--i;
			--count;
		}
	}
	return result.normalized();
}

WorkingIntervals ReplaceDayIntervals(
		const WorkingIntervals &intervals,
		int dayIndex,
		WorkingIntervals replacement) {
	auto result = RemoveDayIntervals(intervals, dayIndex);
	const auto first = result.list.insert(
		end(result.list),
		begin(replacement.list),
		end(replacement.list));
	for (auto &interval : ranges::make_subrange(first, end(result.list))) {
		interval = interval.shifted(dayIndex * kDay);
	}
	return result.normalized();
}

} // namespace Data
