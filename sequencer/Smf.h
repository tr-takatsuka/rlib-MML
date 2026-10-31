#pragma once

#include <set>
#include <list>
#include <ostream>
#include <map>
#include <memory>

#include "MidiEvent.h"

namespace rlib::midi {

	class Smf {
	public:
		static constexpr int defaultTimeBase = 480;			// 分解能(4分音符あたりのカウント)
		using Events = std::multimap<size_t, std::shared_ptr<const midi::Event>>;	// <position,Event>
		using Event = Events::value_type;

		class Track {
		public:
			Events	events;
		};
	public:
		int					timeBase = defaultTimeBase;		// 分解能(4分音符あたりのカウント)
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
