#pragma once

#include <set>
#include <list>
#include <ostream>
#include <memory>

#include "MidiEvent.h"

namespace rlib::midi {

	class Smf {
		class Inner;
	public:
		struct Event {
			const uint64_t								position;
			const std::shared_ptr<const midi::Event>	event;

			Event(const Event& e)
				:position(e.position)
				, event(e.event)
			{}
			Event(decltype(position) position_, const std::shared_ptr<const midi::Event> event_)
				:position(position_)
				, event(event_)
			{}

			struct Less {
				typedef void is_transparent;
				bool operator()(const Event& a, const Event& b)				const { return a.position < b.position; }
				bool operator()(const decltype(position) a, const Event& b)	const { return a < b.position; }
				bool operator()(const Event& a, const decltype(position) b)	const { return a.position < b; }
			};

		};

		using Events = std::multiset<Event, Event::Less>;

		class Track {
		public:
			Events	events;
		};
	public:
		int					timeBase = 480;			// 分解能(4分音符あたりのカウント)
		std::list<Track>	tracks;
	public:
		Smf() {}
		Smf(Smf&& smf)
			:timeBase(smf.timeBase)
			, tracks(std::move(smf.tracks))
		{}
		Smf(const Smf& smf)
			:timeBase(smf.timeBase)
			, tracks(smf.tracks)
		{}

		// SMFデータ取得
		std::vector<uint8_t> getFileImage() const;

		friend std::ostream& operator<<(std::ostream& os, const Smf& smf) {
			const auto v = smf.getFileImage();
			os.write(reinterpret_cast<const char*>(v.data()), v.size());
			return os;
		}

		static Smf fromStream(std::istream& is);

		static Smf convertTimebase(const Smf& smf, int timeBase);

	};

}
