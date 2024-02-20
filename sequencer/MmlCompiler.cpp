﻿
#ifndef _MSC_VER
#include <bits/stdc++.h>
#endif

#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>

#include "./MmlCompiler.h"
#include "../stringformat/StringFormat.h"

using namespace rlib;
using namespace rlib::sequencer;

std::string MmlCompiler::Exception::getMessage(Code code) {
	static const std::map<Code, std::string> m = {
		{Code::lengthError,						u8R"(音長の指定に誤りがあります)"},
		{Code::lengthMinusError,				u8R"(音長を負値にはできません)"},
		{Code::commentError,					u8R"(コメント指定に誤りがあります)"},
		{Code::argumentError,					u8R"(関数の引数指定に誤りがあります)"},
		{Code::functionCallError,				u8R"(関数呼び出しに誤りがあります)"},
		{Code::unknownNumberError,				u8R"(数値の指定に誤りがあります)"},
		{Code::vCommandError,					u8R"(ベロシティ指定（v コマンド）に誤りがあります)"},
		{Code::vCommandRangeError,				u8R"(ベロシティ指定（v コマンド）の値が範囲外です)"},
		{Code::lCommandError,					u8R"(デフォルト音長指定（l コマンド）に誤りがあります)"},
		{Code::oCommandError,					u8R"(オクターブ指定（o コマンド）に誤りがあります)"},
		{Code::oCommandRangeError,				u8R"(オクターブ指定（o コマンド）の値が範囲外です)"},
		{Code::tCommandRangeError,				u8R"(テンポ指定（t コマンド）に誤りがあります)"},
		{Code::programchangeCommandError,		u8R"(音色指定（@ コマンド）に誤りがあります)"},
		{Code::rCommandRangeError,				u8R"(休符指定（r コマンド）に誤りがあります)"},
		{Code::noteCommandRangeError,			u8R"(音符指定（a～g コマンド）に誤りがあります)"},
		{Code::octaveUpDownCommandError,		u8R"(オクターブアップダウン（ < , > コマンド）に誤りがあります)"},
		{Code::octaveUpDownRangeCommandError,	u8R"(オクターブ値が範囲外です)" },
		{Code::tieCommandError,					u8R"(タイ（^ コマンド）に誤りがあります)"},
		{Code::createPortPortNameError,			u8R"(CreatePort コマンドのポート名指定に誤りがあります)"},
		{Code::createPortDuplicateError,		u8R"(CreatePort コマンドでポート名が重複しています)" },
		{Code::createPortChannelError,			u8R"(CreatePort コマンドのチャンネル指定に誤りがあります)" },
		{Code::portNameError,					u8R"(Port コマンドのポート名指定に誤りがあります)" },
		{Code::volumeError,						u8R"(Volume コマンドの指定に誤りがあります)" },
		{Code::volumeRangeError,				u8R"(Volume コマンドの値が範囲外です)" },
		{Code::panError,						u8R"(Pan コマンドの指定に誤りがあります)" },
		{Code::panRangeError,					u8R"(Pan コマンドの値が範囲外です)" },
		{Code::pitchBendError,					u8R"(PitchBend コマンドの指定に誤りがあります)" },
		{Code::pitchBendRangeError,				u8R"(PitchBend コマンドの値が範囲外です)" },
		{Code::controlChangeError,				u8R"(ControlChange コマンドの指定に誤りがあります)" },
		{Code::controlChangeRangeError,			u8R"(ControlChange コマンドの値が範囲外です)" },
		{Code::unknownError,					u8R"(解析出来ない書式です)"},
		{Code::stdEexceptionError,				u8R"(std::excption エラーです)"},
	};
	if (auto i = m.find(code); i != m.end()) {
		return i->second;
	}
	assert(false);
	return "unknown";
}

class MmlCompiler::Inner
{
public:
	static const std::string sRegexLength;

	static std::optional<boost::smatch> regexSearch(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd, const boost::regex& re) {
		// match_not_dot_newline  : . は改行以外にマッチ
		// match_single_line	  : ^ は先頭行の行頭のみにマッチ
		// boost::match_continuous: 先頭から始まる部分シーケンスにのみマッチすることを指定する
		boost::smatch m;
		if (boost::regex_search(iBegin, iEnd, m, re, boost::match_not_dot_newline | boost::match_single_line | boost::match_continuous)) {
			return m;
		}
		return std::nullopt;
	}

	// 音長文字列解析
	static size_t parseLength(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd, size_t defaultLength) {
		long long result = 0;

		auto i = iBegin;
		std::string::const_iterator itBefore;
		do {
			itBefore = i;

			// + or -
			const bool plus = [&] {
				if (auto r = regexSearch(i, iEnd, boost::regex(R"(^\s*([\+\-]?))"))) {
					i = (*r)[0].second;
					if ((*r)[1].str() == "-") {
						return false;
					}
				}
				return true;
			}();

			// ステップ数指定か?
			const bool stepNotation = [&] {
				if (auto r = regexSearch(i, iEnd, boost::regex(R"(^\s*\!)"))) {
					i = (*r)[0].second;
					return true;
				}
				return false;
			}();

			size_t tmpStep = defaultLength;

			// 数値
			if (auto r = regexSearch(i, iEnd, boost::regex(R"(^\s*([0-9]+))"))) {
				size_t n = boost::lexical_cast<size_t>((*r)[1].str());
				if (stepNotation) {
					tmpStep = n;
				} else {
					tmpStep = (timeBase * 4) / n;
				}
				i = (*r)[0].second;
			} else {
				if (stepNotation) {		// ステップ数指定しておきながら数値がないなら
					throw Exception(Exception::Code::lengthError, i, iEnd);
				}
			}

			{// 付点音符
				if (auto r = regexSearch(i, iEnd, boost::regex(R"(^(\s*\.)+)"))) {
					const size_t n = std::count((*r)[0].first, (*r)[0].second, '.');
					size_t t = tmpStep / 2;
					for (size_t j = 0; j < n; j++, t /= 2) {
						tmpStep += t;
					}
					i = (*r)[0].second;
				}
			}

			result += tmpStep * (plus ? 1 : -1);

		} while (i != iEnd && i != itBefore);

		if (result < 0) {		// 負値はエラー
			throw Exception(Exception::Code::lengthMinusError, iBegin, iEnd);
		}

		return result;
	}

	// コメント解析
	static std::optional<std::string::const_iterator> parseCommant(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
#if 1
		if (*iBegin != '/') return std::nullopt;	// 高速化のために最初の1文字だけでまずチェック
		static const boost::regex re(R"(^((?<line>\/\/)|(?<range>\/\*)))");
#else
		static const boost::regex re(R"(^\s*((?<line>\/\/)|(?<range>\/\*)))");
#endif
		if (auto r = regexSearch(iBegin, iEnd, re)) {
			const auto& re = [&] {
				if (const auto& sm = (*r)["line"]; sm.matched) {	// 次行or終端 まで飛ばす
					const static boost::regex re(R"(^.*(\r\n|\n|\r|$))");
					return re;
				}
				if (const auto& sm = (*r)["range"]; sm.matched) {	// */ まで飛ばす
					const static boost::regex re(R"(^((?!\*\/)[\s\S])*\*\/)");
					return re;
				}
				assert(false);
				throw Exception(Exception::Code::commentError, iBegin, iEnd);
			}();
			if (auto r2 = regexSearch((*r)[0].second, iEnd, re)) {
				return (*r2)[0].second;
			}
			throw Exception(Exception::Code::commentError, iBegin, iEnd);
		}
		return std::nullopt;
	};

	// 数値解析
	static auto parseInt(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd,bool sign) {
		struct Result {
			std::string::const_iterator		next;		// 次の位置
			std::optional<bool>				sign;		// true:+ false:-
			long long						value = 0;
		};
		std::optional<Result> result = Result();

		auto const &re = [&]{
			if(sign){
				static const boost::regex re(R"(^\s*(?<value>(?<sign>[\+\-]?)[0-9]+))");
				return re;
			}
			static const boost::regex re(R"(^\s*(?<value>[0-9]+))");
			return re;
		}();
		if (auto r = regexSearch(iBegin, iEnd, re)) {
			result->next = (*r)[0].second;
			result->value = boost::lexical_cast<long long>((*r)["value"]);
			if( auto sign = (*r)["sign"].str(); !sign.empty() ){
				result->sign = sign == "+" ? true : false;
			}
			return result;
		}
		return decltype(result)();
	}

	// 関数解析
	static auto parseFunction(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd, const std::string& functionName) {
		struct Result {
			using PairIterator = std::pair<std::string::const_iterator, std::string::const_iterator>;
			PairIterator						functionName;	// 関数名
			std::string::const_iterator			next;			// 次の位置
			std::map<std::string, PairIterator>	args;			// 引数リスト

			std::optional<std::string> findArgString(const std::string name)const {
				if (auto i = args.find(name); i != args.end()) return std::string(i->second.first, i->second.second);
				return std::nullopt;
			}

			auto findArgInt(const std::string name, bool sign)const {
				struct Result {
					std::optional<bool>	sign;		// true:+ false:-
					long long			value = 0;
				};
				std::optional<Result> result = Result();

				if (const auto i = args.find(name); i != args.end()) {
					if (const auto r = parseInt(i->second.first, i->second.second, sign)) {
						if (r->next == i->second.second){		// 数値以外がある?
							result->sign = r->sign;
							result->value = r->value;
							return result;
						}
					}
				}
				return decltype(result)();
			}
		};
		std::optional<Result> result = Result();

		std::string::const_iterator it;
		if (auto r = regexSearch(iBegin, iEnd, boost::regex(string::format(R"(^\s*(?<name>%1%)\W)", functionName)))) {
			result->functionName = (*r)["name"];		// ヒットした関数名
			it = (*r)["name"].second;
		} else {
			return decltype(result)();		// 該当しなかった
		}

		while (auto r = Inner::parseCommant(it, iEnd)) it = *r;	// コメントを読み飛ばす

		if (auto r = regexSearch(it, iEnd, boost::regex(R"(^\s*\()"))) {		// "(" 検索
			it = (*r)[0].second;
		} else {
			return decltype(result)();		// 該当しなかった
		}

		std::vector<decltype(result->args)::value_type::second_type> nonameArgs;

		std::string currentArgName;
		union {									// 次トークン情報
			struct {
				uint16_t	rightParen : 1;		// )
				uint16_t	comma : 1;			// ,
				uint16_t	arg : 2;			// 1:引数名or名前ナシ引数値 2:引数値
			};
			uint16_t		all = 0;
		}flags;
		flags.rightParen = true;
		flags.arg = 1;

		std::string::const_iterator itBefore;
		do {
			itBefore = it;

			if (auto r = Inner::parseCommant(it, iEnd)) {	// コメント
				it = *r;
				continue;
			}

			if (flags.rightParen) {							// )
				static const boost::regex re(R"(^\s*\))");
				if (auto r = regexSearch(it, iEnd, re)) {
					for (size_t i = 0; i < nonameArgs.size(); i++) {
						result->args[boost::lexical_cast<std::string>(i)] = nonameArgs[i];
					}
					result->next = (*r)[0].second;		// 次の位置
					return result;
				}
			}

			if (flags.comma) {								// ,
				static const boost::regex re(R"(^\s*\,)");
				if (auto r = regexSearch(it, iEnd, re)) {
					it = (*r)[0].second;
					flags.all = 0;
					flags.rightParen = true;
					flags.arg = 1;
					continue;
				}
			}

			if (flags.arg == 1) {		// 1:引数名or名前ナシ引数値
				if (auto r = regexSearch(it, iEnd, boost::regex(R"(^\s*([a-zA-Z]\w*))"))) {
					const auto& mName = *r;
					auto i = mName[0].second;
					while (auto r = Inner::parseCommant(i, iEnd)) i = *r;	// コメントを読み飛ばす
					if (auto r = regexSearch(i, iEnd, boost::regex(R"(^\s*\:)"))) {	// ":" がある？
						currentArgName = mName[1].str();
						it = (*r)[0].second;
						flags.all = 0;
						flags.arg = 2;
						continue;
					}
				}
				if (auto r = regexSearch(it, iEnd, boost::regex(R"(^\s*(?<value>((?![,\)])\S)*)[,\)])"))) {	// 引数値（ "," ")" まで ）
					if (const auto& sm = (*r)["value"]; sm.matched) {
						nonameArgs.push_back(sm);
						it = sm.second;
						currentArgName.clear();
						flags.all = 0;
						flags.comma = true;
						flags.rightParen = true;
						continue;
					}
				}
				throw Exception(Exception::Code::argumentError, it, iEnd);
			} else if (flags.arg == 2) {		// 2:引数値
				if (auto r = regexSearch(it, iEnd, boost::regex(R"(^\s*R"(?<key>\w*)\()"))) {		// 生文字リテラル
					const auto key = (*r)["key"].str();
					const auto re = string::format(R"(^(?<value>(?!\)%1%")[\s\S]*)\)%1%")", key);
					if (auto r2 = regexSearch((*r)[0].second, iEnd, boost::regex(re))) {			// 値取得
						result->args[currentArgName] = (*r2)["value"];
						it = (*r2)[0].second;
					} else {
						throw Exception(Exception::Code::argumentError, it, iEnd);
					}
				} else if (auto r = regexSearch(it, iEnd, boost::regex(R"(^\s*(?<value>((?![,\)])\S)*)\s*[,\)])"))) {	// 引数値（ "," ")" まで ）
					if (const auto& sm = (*r)["value"]; sm.matched) {
						result->args[currentArgName] = sm;
						it = sm.second;
					} else {
						throw Exception(Exception::Code::argumentError, it, iEnd);
					}
				} else {
					throw Exception(Exception::Code::argumentError, it, iEnd);
				}
				currentArgName.clear();
				flags.all = 0;
				flags.comma = true;
				flags.rightParen = true;
				continue;
			}

		} while (it != iEnd && it != itBefore);
		throw Exception(Exception::Code::functionCallError, it, iEnd);
	};

	static std::string getErrorPostionString(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
		// UTF8 文字列から先頭の１文字取得
		static const auto getUtf8Char = [](const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) -> std::optional<std::smatch> {
			static const std::regex re(u8R"([\x00-\x7f]|[\xc2-\xdf][\x80-\xbf]{1}|[\xe0-\xef][\x80-\xbf]{2}|[\xf0-\xf4][\x80-\xbf]{3})");
			std::smatch m;
			if (std::regex_search(iBegin, iEnd, m, re)) {
				return m;
			}
			return std::nullopt;
		};
		std::string r;
		int count = 8;			// 必要文字数
		for (std::string::const_iterator i = iBegin; i != iEnd && count >= 0; count--) {
			auto m = getUtf8Char(i, iEnd);
			if (!m) break;
			r += (*m)[0].str();
			i = (*m)[0].second;
		}
		return r;
	}

	struct Port {
		size_t						position = 0;			// 現在の位置
		size_t						defaultStep = 480;		// デフォルト音長(step)
		int							octave = 4;				// 現在のオクターブ( -2 ～ 8 )
		int							velocity = 100;			// 現在のベロシティ(0～127)
		int							volume = 100;			// 現在のボリューム(0～127)
		int							pan = 64;				// 現在のパン(0～127)
		std::shared_ptr<EventNote>	beforeEvent;			// 直前の音符( ^の対象)
		bool						noteUnmove = false;		// Noteで現在位置を進めないモード
		MmlCompiler::Port			port;
	};

	struct State {
		std::map<std::string, Port>	mapPort;
		std::string					currentPortName;

		// v? ベロシティ
		const std::optional<std::string::const_iterator> parseVelocity(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*(?<command>v)[^a-zA-Z])");
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				const auto v = parseInt((*r)["command"].second, iEnd, true);
				if (v) {
					auto& port = mapPort[currentPortName];
					if( v->sign ){		// 符号付きなら相対指定
						auto n = port.velocity + v->value;
						port.velocity = static_cast<decltype(port.velocity)>((std::min)((std::max)(n, 0LL), 127LL));
					}else{
						auto n = v->value;
						if (n < 0 || n > 127) {		// 範囲チェック
							throw Exception(Exception::Code::vCommandRangeError, iBegin, iEnd);
						}
						port.velocity = static_cast<decltype(port.velocity)>(n);
					}
					return v->next;
				}
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::vCommandError, iBegin, iEnd);
		};

		// l?? デフォルト音長
		const std::optional<std::string::const_iterator> parseDefaultLength(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*l\s*((\!?[0-9]+\.*)(\s*[\+\-]\!?[0-9]+\.*)*))");
				const auto r = Inner::regexSearch(iBegin, iEnd, re);		// 数字以外があるなら
				if (!r) {
					return std::nullopt;
				}
				if (const auto& sm = (*r)[1]; sm.matched) {
					auto& port = mapPort[currentPortName];
					auto step = Inner::parseLength(sm.first, sm.second, port.defaultStep);
					port.defaultStep = step;
					return (*r)[0].second;
				}
				assert(false);
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::lCommandError, iBegin, iEnd);
		};

		// o?? オクターブ
		const std::optional<std::string::const_iterator> parseOctave(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*o\s*(?<value>\-?[0-9]+))");
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				auto& port = mapPort[currentPortName];
				if (const auto& sm = (*r)["value"]; sm.matched) {
					int octave = boost::lexical_cast<int>(sm.str());
					if (octave < -2 || octave > 8) {		// 範囲チェック
						throw Exception(Exception::Code::oCommandRangeError, iBegin, iEnd);
					}
					port.octave = octave;
					port.beforeEvent.reset();				// '^'の対象をクリア
					return (*r)[0].second;
				}
				assert(false);
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::oCommandError, iBegin, iEnd);
		};

		// t?? テンポ
		const std::optional<std::string::const_iterator> parseTempo(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*t\s*(?<value>\+?[0-9]+(\.[0-9]*)?([eE][\+-]?[0-9]+)?))");
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				if (const auto& sm = (*r)["value"]; sm.matched) {
					double tempo = boost::lexical_cast<double>(sm.str());

					auto& port = mapPort[currentPortName];
					auto e = std::make_shared<EventTempo>();
					e->position = port.position;
					e->tempo = tempo;
					port.port.eventList.insert(e);

					return (*r)[0].second;
				}
				assert(false);
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::tCommandRangeError, iBegin, iEnd);
		};

		// @?? プログラムチェンジ
		const std::optional<std::string::const_iterator> parseProgramChange(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*@\s*(?<value>[0-9]+))");
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				if (const auto& sm = (*r)["value"]; sm.matched) {
					int programNo = boost::lexical_cast<int>(sm.str());

					auto& port = mapPort[currentPortName];
					auto e = std::make_shared<EventProgramChange>();
					e->position = port.position;
					e->programNo = programNo;
					port.port.eventList.insert(e);

					return (*r)[0].second;
				}
				assert(false);
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::programchangeCommandError, iBegin, iEnd);
		};

		// r?? 休符
		const std::optional<std::string::const_iterator> parseRest(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^(?<rest>\s*r\s*))" + Inner::sRegexLength);
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				auto& port = mapPort[currentPortName];
				auto step = Inner::parseLength((*r)["rest"].second, (*r)[0].second, port.defaultStep);
				port.position += step;
				port.beforeEvent.reset();				// '^'の対象をクリア
				return (*r)[0].second;
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::rCommandRangeError, iBegin, iEnd);
		};

		// a～g?? 音符
		const std::optional<std::string::const_iterator> parseNote(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*(?<note>[a-g][\+\-]?)\s*)" + Inner::sRegexLength);
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				auto& port = mapPort[currentPortName];
				auto step = Inner::parseLength((*r)["note"].second, (*r)[0].second, port.defaultStep);
				auto e = std::make_shared<EventNote>();
				e->position = port.position;
				e->note = [&] {
					static const std::map<std::string, int> noteTable{
						{"c",	0},	{"c-",	-1},{"c+",	1},
						{"d",	2},	{"d-",	1},	{"d+",	3},
						{"e",	4},	{"e-",	3},	{"e+",	5},
						{"f",	5},	{"f-",	4},	{"f+",	6},
						{"g",	7},	{"g-",	6},	{"g+",	8},
						{"a",	9},	{"a-",	8},	{"a+",	10},
						{"b",	11},{"b-",	10},{"b+",	12},
					};
					if (auto i = noteTable.find((*r)["note"].str()); i != noteTable.end()) {
						int note = (port.octave + 2) * 12 + i->second;
						if (note >= 0 && note <= 127) {		// 範囲チェック
							return note;
						}
					}
					throw Exception(Exception::Code::noteCommandRangeError, iBegin, iEnd);
				}();
				e->length = step;
				e->velocity = port.velocity;
				port.port.eventList.insert(e);
				if (!port.noteUnmove) {
					port.position += step;
				}
				port.beforeEvent = e;
				return (*r)[0].second;
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::noteCommandRangeError, iBegin, iEnd);
		};

		// < > オクターブUPDOWN
		const std::optional<std::string::const_iterator> parseOctaveUpDown(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*([<>]))");
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				auto& port = mapPort[currentPortName];
				if ((*r)[1].str() == "<") {	// UP
					port.octave++;
				} else {					// DOWN
					port.octave--;
				}
				if (port.octave < -2 || port.octave > 8) {	// 範囲チェック
					throw Exception(Exception::Code::octaveUpDownRangeCommandError, iBegin, iEnd);
				}
				port.beforeEvent.reset();				// '^'の対象をクリア
				return (*r)[0].second;
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::octaveUpDownCommandError, iBegin, iEnd);
		};

		// ^ tie (長さを付け足す)
		const std::optional<std::string::const_iterator> parseTie(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
#if 1
				if (*iBegin != '^') return std::nullopt;	// 高速化のために最初の1文字だけでまずチェック
				static const boost::regex re(R"(^(\^\s*))" + Inner::sRegexLength);
#else
				static const boost::regex re(R"(^(\s*\^\s*))" + Inner::sRegexLength);
#endif
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				auto& port = mapPort[currentPortName];
				auto step = Inner::parseLength((*r)[1].second, (*r)[0].second, port.defaultStep);
				if (port.beforeEvent) {					// 直前の音符があれば
					port.beforeEvent->length += step;	// 音符の音長に足す
				}
				if (!port.noteUnmove || !port.beforeEvent) {
					port.position += step;
				}
				return (*r)[0].second;
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::tieCommandError, iBegin, iEnd);
		};

		// ' Noteで現在位置を進めるか否かモード
		const std::optional<std::string::const_iterator> parseNoteUnmove(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
			try {
				static const boost::regex re(R"(^\s*('))");
				const auto r = Inner::regexSearch(iBegin, iEnd, re);
				if (!r) {
					return std::nullopt;
				}
				auto& port = mapPort[currentPortName];
				port.noteUnmove = !port.noteUnmove;		// 反転
				port.beforeEvent.reset();				// '^'の対象をクリア
				return (*r)[0].second;
			} catch (const Exception& e) {
				throw e;
			} catch (...) {
			}
			throw Exception(Exception::Code::octaveUpDownCommandError, iBegin, iEnd);
		};

	};

};

const std::string MmlCompiler::Inner::sRegexLength = R"((?<len>(\!?[0-9]+)?\.*)?(\s*[\+\-]\!?[0-9]+\.*)*)";

MmlCompiler::Exception::Exception(Code code_, const std::string::const_iterator& itBegin, const std::string::const_iterator& itEnd, const std::string& annotate_)
	:std::runtime_error(getMessage(code_))
	, code(code_)
	, errorWord(Inner::getErrorPostionString(itBegin, itEnd))
	, it(itBegin)
	, annotate(annotate_)
{
}

MmlCompiler::Result MmlCompiler::compile(const std::string& mml) {

	using namespace std;

	Inner::State state;
	std::string::const_iterator	it = mml.cbegin();

	try {
		while (true) {

			// 先頭の空白(改行タブ等含む)を飛ばす
			it = std::find_if(it, mml.cend(), [](const auto& ch) {return !std::isspace(ch); });

			// 完了
			if (it == mml.cend()) break;

			if (auto r = Inner::parseCommant(it, mml.cend())) {						// コメント
				it = *r;
				continue;
			}

			if (auto r = state.parseTie(it, mml.cend())) {							// ^ tie (長さを付け足す)
				it = (*r);
				continue;
			}
			if (auto r = state.parseOctaveUpDown(it, mml.cend())) {					// < > オクターブUPDOWN
				it = (*r);
				continue;
			}
			if (auto r = state.parseNoteUnmove(it, mml.cend())) {					// ' noteで位置更新するか否かモード
				it = (*r);
				continue;
			}
			if (auto r = state.parseProgramChange(it, mml.cend())) {				// @? プログラムチェンジ
				it = (*r);
				continue;
			}

			if (auto r = Inner::parseFunction(it, mml.cend(), "createPort|CreatePort")) {		// CreatePort
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::createPortPortNameError, it, mml.cend());
				}
				if (auto r2 = state.mapPort.insert({ name ,Inner::Port() }); !r2.second) {			// port追加 
					throw Exception(Exception::Code::createPortDuplicateError, it, mml.cend());
				} else {
					auto& port = r2.first->second;
					const auto ch = r->findArgInt("channel", false);
					if( !ch || ch->value < 1 || ch->value > 16 ){
						throw Exception(Exception::Code::createPortChannelError, it, mml.cend());
					}
					port.port.channel = static_cast<decltype(port.port.channel)>(ch->value - 1);
				}

				it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(it, mml.cend(), "port|Port")) {			// Port
				const auto name = (*r).findArgString("0").value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::portNameError, it, mml.cend());
				}
				if (const auto i = state.mapPort.find(name); i == state.mapPort.end()) {	// 存在しないportならエラー
					throw Exception(Exception::Code::portNameError, it, mml.cend());
				}
				state.currentPortName = name;
				it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(it, mml.cend(), "volume|Volume|V")) {		// Volume
				const auto v = r->findArgInt("0", true);
				if (!v) {
					throw Exception(Exception::Code::volumeError, it, mml.cend());
				}
				auto& port = state.mapPort[state.currentPortName];
				if( v->sign ){		// 符号があるなら相対指定
					auto n = port.volume + v->value;
					port.volume = static_cast<decltype(port.volume)>((std::min)((std::max)(n, 0LL), 127LL));
				}else{
					if (v->value < 0 || v->value > 127) {
						throw Exception(Exception::Code::volumeRangeError, it, mml.cend());
					}
					port.volume = static_cast<decltype(port.volume)>(v->value);
				}

				auto e = make_shared<EventVolume>();
				e->position = port.position;
				e->volume = port.volume;
				port.port.eventList.insert(e);

				it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(it, mml.cend(), "pan|Pan")) {		// Pan
				const auto v = r->findArgInt("0", true);
				if (!v) {
					throw Exception(Exception::Code::panError, it, mml.cend());
				}
				auto& port = state.mapPort[state.currentPortName];
				if (v->sign) {		// 符号があるなら相対指定
					auto n = port.pan + v->value;
					port.pan = static_cast<decltype(port.pan)>((std::min)((std::max)(n, 0LL), 127LL));
				} else {
					if (v->value < 0 || v->value > 127) {
						throw Exception(Exception::Code::panRangeError, it, mml.cend());
					}
					port.pan = static_cast<decltype(port.pan)>(v->value);
				}

				auto e = make_shared<EventPan>();
				e->position = port.position;
				e->pan = port.pan;
				port.port.eventList.insert(e);

				it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(it, mml.cend(), "pitchBend|PitchBend")) {		// PitchBend
				const auto v = r->findArgInt("0", true);
				if (!v) {
					throw Exception(Exception::Code::pitchBendError, it, mml.cend());
				}
				auto& port = state.mapPort[state.currentPortName];
				if (v->value < -8192 || v->value > 8191) {
					throw Exception(Exception::Code::pitchBendRangeError, it, mml.cend());
				}
				auto e = make_shared<EventPitchBend>();
				e->position = port.position;
				e->pitchBend = static_cast<decltype(e->pitchBend)>(v->value);
				port.port.eventList.insert(e);

				it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(it, mml.cend(), "controlChange|ControlChange|CC")) {	// ControlChange
				const auto no = [&] {
					if (auto n = r->findArgInt("0", false)) return n;
					return r->findArgInt("no", false);
				}();
				const auto val = [&] {
					if (auto n = r->findArgInt("1", false)) return n;
					return r->findArgInt("value", false);
				}();
				if (!no || !val) {
					throw Exception(Exception::Code::controlChangeError, it, mml.cend());
				}
				if (no->value < 0 || no->value > 127 || val->value < 0 || val->value > 127) {
					throw Exception(Exception::Code::controlChangeRangeError, it, mml.cend());
				}
				auto& port = state.mapPort[state.currentPortName];

				auto e = make_shared<EventControlChange>();
				e->position = port.position;
				e->no = static_cast<decltype(e->no)>(no->value);
				e->value = static_cast<decltype(e->no)>(val->value);
				port.port.eventList.insert(e);

				it = r->next;
				continue;
			}

			if (auto r = state.parseRest(it, mml.cend())) {							// r 休符
				it = (*r);
				continue;
			}
			if (auto r = state.parseNote(it, mml.cend())) {							// a～g 音符
				it = (*r);
				continue;
			}
			if (auto r = state.parseVelocity(it, mml.cend())) {						// v? ベロシティ
				it = (*r);
				continue;
			}
			if( auto r = state.parseDefaultLength(it, mml.cend()) ){				// l? デフォルト音長
				it = (*r);
				continue;
			}
			if (auto r = state.parseOctave(it, mml.cend())) {						// o? オクターブ
				it = (*r);
				continue;
			}
			if (auto r = state.parseTempo(it, mml.cend())) {						// t? テンポ
				it = (*r);
				continue;
			}

#if 1
			throw Exception(Exception::Code::unknownError, it, mml.cend());
#else
			static const boost::regex re(R"(^\s*(\S+))");
			boost::smatch match;
			if (boost::regex_search(it, mml.cend(), match, re, boost::match_not_dot_newline | boost::match_single_line)) {
				if (match[1].matched) {
					throw Exception(Exception::Code::unknownError, match[1].first, mml.cend());
				}
				break;
			}

			break;		// 完了
#endif
		}

		Result result;
		for (auto& i : state.mapPort) {
			result.emplace_back(std::move(i.second.port));
		}
		return result;

	} catch (const Exception& e) {
		throw e;
	} catch (const std::exception& e) {
		throw Exception(Exception::Code::stdEexceptionError, it, mml.cend(), e.what());
	} catch (...) {
		throw Exception(Exception::Code::unknownError, it, mml.cend());
	}
}
