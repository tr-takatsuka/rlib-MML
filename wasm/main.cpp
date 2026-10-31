#include <stdio.h>
#include <emscripten.h>
#include <emscripten/bind.h>

#include <iostream>
#include <memory>
#include <vector>
#include <sstream>
#include <regex>
#include <exception>

#include "../stringformat/StringFormat.h"
#include "../sequencer/MmlToSmf.h"
#include "../sequencer/SmfToMml.h"
#include "../sequencer/Timeline.h"

emscripten::val mmlToSmf(const std::string& mml) {
	auto ret = emscripten::val::object();
	try {
		// std::cout << "mmlToSmf" << std::endl;
		const auto r = rlib::sequencer::mmlToSmf(mml);
		if (r.hasError()) {
			const auto errorJson = r.mmlResult.getJson(r.mmlResult.errors);
			ret.set("error", errorJson);
		} else {
			const auto v = r.smf.getFileImage();
			const auto uint8Array = emscripten::val::global("Uint8Array").new_(v.size());
			uint8Array.call<void>("set", emscripten::typed_memory_view(v.size(), v.data()));	// wasm側のメモリをコピー
			ret.set("result", uint8Array);
		}
	} catch (const std::exception& e) {
		// std::cout << "mmlToSmf std::exception" << std::endl;
		ret.set("error", emscripten::val::u8string(e.what()));
	} catch (...) {
		// std::cout << "mmlToSmf unknown exception" << std::endl;
		ret.set("error", emscripten::val::u8string("unknown exception"));
	}
	return ret;
}



emscripten::val smfToMml(const std::string& smfBinary) {
	auto ret = emscripten::val::object();
	try {
		// std::cout << "smfToMml" << std::endl;
		std::istringstream is(smfBinary, std::istringstream::binary);
		auto smf = rlib::midi::Smf::fromStream(is);
		const auto mml = rlib::sequencer::smfToMml(smf);
		ret.set("result", emscripten::val::u8string(mml.c_str()));
		ret.set("ok", true);
	} catch (std::exception& e) {
		// std::cout << "smfToMml std::exception" << std::endl;
		ret.set("message", emscripten::val::u8string(e.what()));
		ret.set("ok", false);
	} catch (...) {
		// std::cout << "smfToMml unknown exception" << std::endl;
		ret.set("message", emscripten::val::u8string("unknown exception"));
		ret.set("ok", false);
	}
	return ret;
}

// SMF を Track(チャンネル+ポートごと。NoteOn と NoteOff は音長付きの1つのノート)の一覧にして、JSON 文字列で返す。JS 側で JSON.parse する。
emscripten::val getTracks(const std::string& smfBinary) {
	auto ret = emscripten::val::object();
	try {
		std::istringstream is(smfBinary, std::istringstream::binary);
		const auto smf = rlib::midi::Smf::fromStream(is);
		const auto tracks = rlib::sequencer::smfToTracks(smf);
		ret.set("result", emscripten::val::u8string(rlib::sequencer::MmlCompiler::Util::tracksToJson(tracks).c_str()));
	} catch (const std::exception& e) {
		ret.set("error", emscripten::val::u8string(e.what()));
	} catch (...) {
		ret.set("error", emscripten::val::u8string("unknown exception"));
	}
	return ret;
}

// SMF の 位置(tick)/小節位置(Location)/時間(秒) の相互変換。JS 側では dispose()(embind の delete())で解放する。
class TimelineJs {
public:
	rlib::midi::Timeline timeline;

	explicit TimelineJs(rlib::midi::Timeline&& t)
		:timeline(std::move(t))
	{}

	static emscripten::val create(const std::string& smfBinary) {
		auto ret = emscripten::val::object();
		try {
			std::istringstream is(smfBinary, std::istringstream::binary);
			const auto smf = rlib::midi::Smf::fromStream(is);
			auto r = rlib::midi::Timeline::fromSmf(smf);	// r.endTick は JS 側では使わない(終端は呼び出し側で管理する)
			TimelineJs t(std::move(r.timeline));
			ret.set("result", std::move(t));	// 登録済みクラスなので JS 側でインスタンスになる
		} catch (const std::exception& e) {
			ret.set("error", emscripten::val::u8string(e.what()));
		} catch (...) {
			ret.set("error", emscripten::val::u8string("unknown exception"));
		}
		return ret;
	}

	static emscripten::val toVal(const rlib::midi::MeterList::Location& loc) {
		auto v = emscripten::val::object();
		v.set("measure", static_cast<int>(loc.measure));
		v.set("beat", static_cast<int>(loc.beat));
		v.set("tick", static_cast<int>(loc.tick));
		return v;
	}
	static rlib::midi::MeterList::Location fromVal(const emscripten::val& v) {
		const auto get = [&](const char* name) {
			const double d = v[name].as<double>();
			return d > 0 ? d : 0.0;		// 負は 0
		};
		rlib::midi::MeterList::Location loc;
		loc.measure = static_cast<uint16_t>(std::min(get("measure"), 65535.0));
		loc.beat = static_cast<uint8_t>(std::min(get("beat"), 255.0));
		loc.tick = static_cast<uint16_t>(std::min(get("tick"), 65535.0));
		return loc;
	}
	static uint64_t toTick(double tick) {	// 負・NaN は 0
		return tick > 0 ? static_cast<uint64_t>(tick) : 0;
	}

	emscripten::val getLocationFromTick(double tick)const { return toVal(timeline.getLocationFromTick(toTick(tick))); }
	double getTickFromLocation(const emscripten::val& loc)const { return static_cast<double>(timeline.getTickFromLocation(fromVal(loc))); }
	emscripten::val getLocationFromTime(double sec)const { return toVal(timeline.getLocationFromTime(sec)); }
	double getTimeFromLocation(const emscripten::val& loc)const { return timeline.getTimeFromLocation(fromVal(loc)); }
	double getTimeFromTick(double tick)const { return timeline.getTimeFromTick(toTick(tick)); }
	double getTickFromTime(double sec)const { return static_cast<double>(timeline.getTickFromTime(sec)); }

	emscripten::val getMeasureTicks(double endTick)const {
		const auto v = timeline.getMeasureTicks(toTick(endTick));
		std::vector<uint32_t> a(v.begin(), v.end());
		const auto arr = emscripten::val::global("Uint32Array").new_(a.size());
		if (!a.empty()) arr.call<void>("set", emscripten::typed_memory_view(a.size(), a.data()));	// wasm側のメモリをコピー
		return arr;
	}
	emscripten::val getSections()const {
		auto arr = emscripten::val::array();
		for (const auto& s : timeline.getSections()) {
			auto o = emscripten::val::object();
			o.set("tick", static_cast<double>(s.tick));
			o.set("measure", static_cast<int>(s.measure));
			o.set("numerator", static_cast<int>(s.numerator));
			o.set("denominator", rlib::midi::MeterList::timeBase * 4 / s.ticksPerBeat);
			o.set("ticksPerBeat", static_cast<int>(s.ticksPerBeat));
			arr.call<void>("push", o);
		}
		return arr;
	}
};

EMSCRIPTEN_BINDINGS(module) {
	emscripten::function("mmlToSmf", &mmlToSmf);
	emscripten::function("smfToMml", &smfToMml);
	emscripten::function("getTracks", &getTracks);
	emscripten::class_<TimelineJs>("Timeline")
		.class_function("create", &TimelineJs::create)
		.function("getLocationFromTick", &TimelineJs::getLocationFromTick)
		.function("getTickFromLocation", &TimelineJs::getTickFromLocation)
		.function("getLocationFromTime", &TimelineJs::getLocationFromTime)
		.function("getTimeFromLocation", &TimelineJs::getTimeFromLocation)
		.function("getTimeFromTick", &TimelineJs::getTimeFromTick)
		.function("getTickFromTime", &TimelineJs::getTickFromTime)
		.function("getMeasureTicks", &TimelineJs::getMeasureTicks)
		.function("getSections", &TimelineJs::getSections);
}
