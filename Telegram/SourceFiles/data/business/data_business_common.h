/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "data/data_location.h"

namespace Data {

class Session;

struct Timezone {
	QString id;
	QString name;
	TimeId utcOffset = 0;

	friend inline bool operator==(
		const Timezone &a,
		const Timezone &b) = default;
};

struct Timezones {
	std::vector<Timezone> list;

	friend inline bool operator==(
		const Timezones &a,
		const Timezones &b) = default;
};;

struct WorkingInterval {
	static constexpr auto kDay = 24 * 3600;
	static constexpr auto kWeek = 7 * kDay;
	static constexpr auto kInNextDayMax = 6 * 3600;

	TimeId start = 0;
	TimeId end = 0;

	explicit operator bool() const {
		return start < end;
	}

	[[nodiscard]] WorkingInterval shifted(TimeId offset) const {
		return { start + offset, end + offset };
	}
	[[nodiscard]] WorkingInterval united(WorkingInterval other) const {
		if (!*this) {
			return other;
		} else if (!other) {
			return *this;
		}
		return {
			std::min(start, other.start),
			std::max(end, other.end),
		};
	}
	[[nodiscard]] WorkingInterval intersected(WorkingInterval other) const {
		const auto result = WorkingInterval{
			std::max(start, other.start),
			std::min(end, other.end),
		};
		return result ? result : WorkingInterval();
	}

	friend inline bool operator==(
		const WorkingInterval &a,
		const WorkingInterval &b) = default;
};

struct WorkingIntervals {
	std::vector<WorkingInterval> list;

	[[nodiscard]] WorkingIntervals normalized() const;

	explicit operator bool() const {
		for (const auto &interval : list) {
			if (interval) {
				return true;
			}
		}
		return false;
	}
	friend inline bool operator==(
		const WorkingIntervals &a,
		const WorkingIntervals &b) = default;
};

struct WorkingHours {
	WorkingIntervals intervals;
	QString timezoneId;

	[[nodiscard]] WorkingHours normalized() const {
		return { intervals.normalized(), timezoneId };
	}

	explicit operator bool() const {
		return !timezoneId.isEmpty() && !intervals.list.empty();
	}

	friend inline bool operator==(
		const WorkingHours &a,
		const WorkingHours &b) = default;
};

[[nodiscard]] WorkingIntervals ExtractDayIntervals(
	const WorkingIntervals &intervals,
	int dayIndex);
[[nodiscard]] bool IsFullOpen(const WorkingIntervals &extractedDay);
[[nodiscard]] WorkingIntervals RemoveDayIntervals(
	const WorkingIntervals &intervals,
	int dayIndex);
[[nodiscard]] WorkingIntervals ReplaceDayIntervals(
	const WorkingIntervals &intervals,
	int dayIndex,
	WorkingIntervals replacement);

struct BusinessLocation {
	QString address;
	std::optional<LocationPoint> point;

	explicit operator bool() const {
		return !address.isEmpty();
	}

	friend inline bool operator==(
		const BusinessLocation &a,
		const BusinessLocation &b) = default;
};

struct ChatIntro {
	QString title;
	QString description;
	DocumentData *sticker = nullptr;

	[[nodiscard]] bool customPhrases() const {
		return !title.isEmpty() || !description.isEmpty();
	}

	explicit operator bool() const {
		return customPhrases() || sticker;
	}

	friend inline bool operator==(
		const ChatIntro &a,
		const ChatIntro &b) = default;
};

struct BusinessDetails {
	WorkingHours hours;
	BusinessLocation location;
	ChatIntro intro;

	explicit operator bool() const {
		return hours || location || intro;
	}

	friend inline bool operator==(
		const BusinessDetails &a,
		const BusinessDetails &b) = default;
};

[[nodiscard]] BusinessDetails FromMTP(
	not_null<Session*> owner,
	const tl::conditional<MTPBusinessWorkHours> &hours,
	const tl::conditional<MTPBusinessLocation> &location,
	const tl::conditional<MTPBusinessIntro> &intro);

} // namespace Data
