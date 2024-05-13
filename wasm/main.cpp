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


emscripten::val mmlToSmf(const std::string& mml) {
	auto ret = emscripten::val::object();
	try {
		std::cout << "mmlToSmf" << std::endl;

		try {
			const auto smf = rlib::sequencer::mmlToSmf(mml);
			std::stringstream ss;
			ss << smf;
			auto s = ss.str();
			ret.set("result", emscripten::val::global("Uint8Array").new_(emscripten::typed_memory_view(s.size(), s.data())));
			ret.set("ok", true);
		} catch (const rlib::sequencer::MmlCompiler::Exception& e) {
			const size_t lineNumber = [&] {	// 行番号取得
				static const std::regex re(R"(\r\n|\n|\r)");
				size_t lf = 0;
				for (auto i = std::sregex_token_iterator(mml.cbegin(), e.it, re, -1); i != std::sregex_token_iterator(); i++) lf++;
				return lf;
			}();
			const auto msg = rlib::sequencer::MmlCompiler::Exception::getMessage(e.code);	// エラーメッセージ取得
			auto errorWord = [&] {
				std::string s(e.errorWord);
				std::smatch m;
				if (std::regex_search(s, m, std::regex(R"(\r\n|\n|\r)"))) {
					s = std::string(s.cbegin(), m[0].first);
				}
				return s;
			}();

			const auto s = rlib::string::format(R"(line %d %s : %s)", lineNumber, errorWord, msg);
			throw std::runtime_error(s);
		}
	} catch (std::exception& e) {
		std::cout << "mmlToSmf std::exception" << std::endl;
		ret.set("message", emscripten::val::u8string(e.what()));
		ret.set("ok", false);
	} catch (...) {
		std::cout << "mmlToSmf unknown exception" << std::endl;
		ret.set("message", emscripten::val::u8string("unknown exception"));
		ret.set("ok", false);
	}

	return ret;
}

emscripten::val smfToMml(const std::string& smfBinary) {
	auto ret = emscripten::val::object();
	try {
		std::cout << "smfToMml" << std::endl;
		std::istringstream is(smfBinary, std::istringstream::binary);
		auto smf = rlib::midi::Smf::fromStream(is);
		const auto mml = rlib::sequencer::smfToMml(smf);
		ret.set("result", emscripten::val::u8string(mml.c_str()));
		ret.set("ok", true);
	} catch (std::exception& e) {
		std::cout << "smfToMml std::exception" << std::endl;
		ret.set("message", emscripten::val::u8string(e.what()));
		ret.set("ok", false);
	} catch (...) {
		std::cout << "smfToMml unknown exception" << std::endl;
		ret.set("message", emscripten::val::u8string("unknown exception"));
		ret.set("ok", false);
	}
	return ret;
}

EMSCRIPTEN_BINDINGS(module) {
	emscripten::function("mmlToSmf", &mmlToSmf);
	emscripten::function("smfToMml", &smfToMml);
}
