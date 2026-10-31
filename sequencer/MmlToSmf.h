#pragma once

#include <typeindex>
#include <typeinfo>

#include "MmlCompiler.h"
#include "Smf.h"

namespace rlib::sequencer {

	struct Result {
		midi::Smf	smf;
		MmlCompiler::Result	mmlResult;
		bool hasError() const { return mmlResult.errors.size() > 0; };
	};
	inline Result mmlToSmf(const std::string& mml) {
		Result r;
		using Smf = midi::Smf;
		r.mmlResult = MmlCompiler::compile(mml);
		if (r.hasError()) return r;
		for (const auto& mmlTrack : r.mmlResult.tracks) {
			Smf::Track track;

			track.events.emplace(Smf::Event(0, std::make_shared<midi::EventMeta>(midi::EventMeta::createText(midi::EventMeta::Type::sequenceName, std::string(mmlTrack.name)))));
			if (!mmlTrack.instrument.empty()) {
				track.events.emplace(Smf::Event(0, std::make_shared<midi::EventMeta>(midi::EventMeta::createText(midi::EventMeta::Type::instrumentName, std::string(mmlTrack.instrument)))));
			}
			if (mmlTrack.port) {	// port 指定があれば、トラック先頭に MIDI Port メタ(FF 21 01 pp)を出力(省略と port:0 は区別する)
				track.events.emplace(Smf::Event(0, std::make_shared<midi::EventMeta>(midi::EventMeta::Type::port, std::vector<uint8_t>{ *mmlTrack.port })));
			}

			for (const auto& event : mmlTrack.eventList) {
				static const std::map<std::type_index, void (*)(Smf::Track&, const MmlCompiler::Track&, const MmlCompiler::Event&)> map = {
					{typeid(MmlCompiler::EventNote), [](Smf::Track& track,const MmlCompiler::Track& mmlTrack,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventNote&>(*event.second);
						track.events.insert(Smf::Event(event.first, std::make_shared<midi::EventNoteOn>(mmlTrack.channel, e.note, e.velocity)));
						track.events.insert(Smf::Event(event.first + e.length, std::make_shared<midi::EventNoteOff>(mmlTrack.channel, e.note, 0)));
					}},
					{typeid(MmlCompiler::EventProgramChange), [](Smf::Track& track,const MmlCompiler::Track& mmlTrack,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventProgramChange&>(*event.second);
						track.events.insert(Smf::Event(event.first, std::make_shared<midi::EventProgramChange>(mmlTrack.channel, e.programNo)));
					}},
					{typeid(MmlCompiler::EventPitchBend), [](Smf::Track& track,const MmlCompiler::Track& mmlTrack,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventPitchBend&>(*event.second);
						track.events.insert(Smf::Event(event.first, std::make_shared<midi::EventPitchBend>(mmlTrack.channel, e.pitchBend)));
					}},
					{typeid(MmlCompiler::EventControlChange), [](Smf::Track& track,const MmlCompiler::Track& mmlTrack,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventControlChange&>(*event.second);
						track.events.insert(Smf::Event(event.first, std::make_shared<midi::EventControlChange>(mmlTrack.channel, e.no, e.value)));
					}},
					{typeid(MmlCompiler::EventMeta), [](Smf::Track& track,const MmlCompiler::Track& mmlTrack,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventMeta&>(*event.second);
						track.events.insert(Smf::Event(event.first, std::make_shared<midi::EventMeta>(static_cast<midi::EventMeta::Type>(e.type), std::move(std::vector<uint8_t>(e.data)))));
					}},
					{typeid(MmlCompiler::EventSystemExclusive), [](Smf::Track& track,const MmlCompiler::Track& mmlTrack,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventSystemExclusive&>(*event.second);
						track.events.insert(Smf::Event(event.first, std::make_shared<midi::EventSystemExclusive>(e.data)));
					}},
				};
				const auto& ev = *event.second;
				if (auto i = map.find(typeid(ev)); i != map.end()) {
					(i->second)(track, mmlTrack, event);
				} else assert(false);
			}
			r.smf.tracks.emplace_back(std::move(track));
		}
		return r;
	}

}
