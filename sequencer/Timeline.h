#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <memory>
#include <limits>

#include "MidiEvent.h"
#include "Smf.h"
#include "TempoList.h"
#include "MeterList.h"

namespace rlib::midi {

	// 「位置(tick) / 小節位置(Location) / 時間(秒)」の相互変換
	template <typename T = double> struct TimelineT {
		using Location = MeterList::Location;

		MeterList	meterList;
		TempoList	tempoList;

		static auto fromSmf(const midi::Smf& smf_) {
			const auto smf = midi::Smf::convertTimebase(smf_, MeterList::timeBase);
			struct {
				TimelineT<T> timeline;
				uint64_t endTick = 0;	// 全トラックで最後のイベントの位置を返す
			}r;
			auto m = MeterList::fromSmf(smf);
			r.timeline.meterList = std::move(m.meterList);
			r.endTick = m.endTick;
			for (const auto& track : smf.tracks) {
				for (const auto& event : track.events) {
					const auto e = std::dynamic_pointer_cast<const midi::EventMeta>(event.second);
					if (!e || e->type != midi::EventMeta::Type::tempo || e->data.size() != 3) continue;
					const double tempo = e->getTempo();
					if (tempo > 0) r.timeline.tempoList.insert(event.first, tempo);
				}
			}
			return r;
		}

		// 位置(tick) から位置(Location)
		Location getLocationFromTick(uint64_t tick)const {
			return meterList.getLocation(tick);
		}
		// 位置(Location)から 位置(tick)
		uint64_t getTickFromLocation(const Location& loc)const {
			return meterList.getTick(loc);
		}
		// 位置(tick) から時間(秒)
		T getTimeFromTick(uint64_t tick)const {
			return tempoList.getTime(tick);
		}
		// 時間(秒)から 位置(tick)
		uint64_t getTickFromTime(T sec)const {
			if (!(sec > 0)) return 0;	// 負・NaN は 0
			return tempoList.getPositionAndTempo(sec).tick;
		}
		// 時間(秒)から位置(Location)
		Location getLocationFromTime(T sec)const {
			return getLocationFromTick(getTickFromTime(sec));
		}
		// 位置(Location)から時間(秒)
		T getTimeFromLocation(const Location& loc)const {
			return getTimeFromTick(getTickFromLocation(loc));
		}

		// 拍子の区間。先頭は必ず tick=0(拍子が無ければ 4/4)。同じ位置なら後勝ち
		struct Section {	// 拍子の区間
			uint64_t	tick;
			uint32_t	measure;
			uint8_t		numerator;
			uint16_t	ticksPerBeat;
		};
		std::vector<Section> getSections()const {
			std::vector<Section> v = { { 0, 0, MeterList::Meter().numerator, MeterList::Meter().ticksPerBeat } };// デフォルト(4/4)
			for (const auto& e : meterList.m_list) {
				const Section s{ e.tick, e.measure, e.meter.numerator, e.meter.ticksPerBeat };
				if (e.tick == v.back().tick) v.back() = s;
				else v.push_back(s);
			}
			return v;
		}

		// 各小節の開始 tick。endTick を含む小節の、次の小節の開始位置まで(最後の小節線まで)
		std::vector<uint64_t> getMeasureTicks(uint64_t endTick)const {
			std::vector<uint64_t> v;
			const auto last = meterList.getLocation(endTick).measure;
			for (decltype(Location::measure) m = 0; m <= last + 1; m++) {
				v.push_back(meterList.getTick(Location{ m, 0, 0 }));
			}
			return v;
		}
	};

	using TimelineF = TimelineT<float>;
	using Timeline = TimelineT<double>;

}
