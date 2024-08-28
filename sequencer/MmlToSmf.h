#pragma once

#include <typeindex>
#include <typeinfo>

#include "MmlCompiler.h"
#include "Smf.h"

namespace rlib::sequencer {

	inline midi::Smf mmlToSmf(const std::string& mml) {
		using Smf = midi::Smf;
		const auto result = MmlCompiler::compile(mml);
		Smf smf;
		for (const auto& port : result) {
			Smf::Track track;

			track.events.emplace(Smf::Event(0, std::make_shared<midi::EventMeta>(midi::EventMeta::createText(midi::EventMeta::Type::sequenceName, port.name))));
			if (!port.instrument.empty()) {
				track.events.emplace(Smf::Event(0, std::make_shared<midi::EventMeta>(midi::EventMeta::createText(midi::EventMeta::Type::instrumentName, port.instrument))));
			}

			for (const auto& event : port.eventList) {
				static const std::map<std::type_index, void (*)(Smf::Track&, const MmlCompiler::Port&, const MmlCompiler::Event&)> map = {
					{typeid(MmlCompiler::EventNote), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventNote&>(event);
						track.events.insert(Smf::Event(e.position, std::make_shared<midi::EventNoteOn>(port.channel, e.note, e.velocity)));
						track.events.insert(Smf::Event(e.position + e.length, std::make_shared<midi::EventNoteOff>(port.channel, e.note, 0)));
					}},
					{typeid(MmlCompiler::EventProgramChange), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventProgramChange&>(event);
						track.events.insert(Smf::Event(e.position, std::make_shared<midi::EventProgramChange>(port.channel, e.programNo)));
					}},
					{typeid(MmlCompiler::EventVolume), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventVolume&>(event);
						track.events.insert(Smf::Event(e.position, std::make_shared<midi::EventControlChange>(port.channel, midi::EventControlChange::Type::volume, e.volume)));
					}},
					{typeid(MmlCompiler::EventPan), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventPan&>(event);
						track.events.insert(Smf::Event(e.position, std::make_shared<midi::EventControlChange>(port.channel, midi::EventControlChange::Type::pan, e.pan)));
					}},
					{typeid(MmlCompiler::EventPitchBend), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventPitchBend&>(event);
						track.events.insert(Smf::Event(e.position, std::make_shared<midi::EventPitchBend>(port.channel, e.pitchBend)));
					}},
					{typeid(MmlCompiler::EventControlChange), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventControlChange&>(event);
						track.events.insert(Smf::Event(e.position, std::make_shared<midi::EventControlChange>(port.channel, e.no, e.value)));
					}},
					{typeid(MmlCompiler::EventTempo), [](Smf::Track& track,const MmlCompiler::Port& port,const MmlCompiler::Event& event) {
						auto& e = static_cast<const MmlCompiler::EventTempo&>(event);
						track.events.insert(Smf::Event(e.position, std::make_shared<midi::EventMeta>(midi::EventMeta::createTempo(e.tempo))));
					}},
				};
				const auto &ev = *event;
				if (auto i = map.find(typeid(ev)); i != map.end()) {
					(i->second)(track, port, *event);
				} else assert(false);
			}
			smf.tracks.emplace_back(std::move(track));
		}

		return smf;
	}

}
