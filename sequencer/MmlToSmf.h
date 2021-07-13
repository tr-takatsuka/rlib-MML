#pragma once

#include "MmlCompiler.h"
#include "Smf.h"

namespace rlib::sequencer {

	inline Smf mmlToSmf(const std::string& mml) {

		const auto result = MmlCompiler::compile(mml);

		Smf smf;

		for (const auto& port : result) {
			Smf::Track track;
			for (const auto& event : port.eventList) {
				static const std::map<std::type_index, std::function<void(Smf::Track&, const MmlCompiler::Port&, const MmlCompiler::Event&)>> map = {
					{typeid(MmlCompiler::EventNote), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventNote&>(event);
						track.events.insert(std::make_shared<Smf::EventNoteOn>(e.position, port.channel, e.note, e.velocity));
						track.events.insert(std::make_shared<Smf::EventNoteOff>(e.position + e.length, port.channel, e.note, 0));
					}},
					{typeid(MmlCompiler::EventProgramChange), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventProgramChange&>(event);
						track.events.insert(std::make_shared<Smf::EventProgramChange>(e.position, port.channel, e.programNo));
					}},
					{typeid(MmlCompiler::EventVolume), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventVolume&>(event);
						track.events.insert(std::make_shared<Smf::EventControlChange>(e.position, port.channel, Smf::EventControlChange::Type::volume, e.volume));
					}},
					{typeid(MmlCompiler::EventPan), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventPan&>(event);
						track.events.insert(std::make_shared<Smf::EventControlChange>(e.position, port.channel, Smf::EventControlChange::Type::pan, e.pan));
					}},
					{typeid(MmlCompiler::EventTempo), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventTempo&>(event);
						auto sp = std::make_shared<Smf::EventMeta>(Smf::EventMeta::createTempo(e.position,e.tempo));
						track.events.insert(sp);
					}},
				};
				if (auto i = map.find(typeid(*event)); i != map.end()) {
					(i->second)(track, port, *event);
				} else assert(false);
			}
			smf.tracks.emplace_back(std::move(track));
		}

		return smf;
	}

}
