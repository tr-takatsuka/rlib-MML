
#ifndef _MSC_VER
#include <bits/stdc++.h>
#endif

#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>

#include "./MmlCompiler.h"
#include "../stringformat/StringFormat.h"

using namespace std;
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
		{Code::createPortPortNameError,			u8R"(createPort コマンドのポート名指定に誤りがあります)"},
		{Code::createPortDuplicateError,		u8R"(createPort コマンドでポート名が重複しています)" },
		{Code::createPortChannelError,			u8R"(createPort コマンドのチャンネル指定に誤りがあります)" },
		{Code::portNameError,					u8R"(port コマンドのポート名指定に誤りがあります)" },
		{Code::volumeError,						u8R"(volume コマンドの指定に誤りがあります)" },
		{Code::panError,						u8R"(pan コマンドの指定に誤りがあります)" },
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

	static optional<boost::smatch> regexSearch(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd, const boost::regex& re) {
		// match_not_dot_newline:  . は改行以外にマッチ
		// match_single_line	:  ^ は先頭行の行頭のみにマッチ
		boost::smatch m;
		if (boost::regex_search(iBegin, iEnd, m, re, boost::match_not_dot_newline | boost::match_single_line)) {
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
	static optional<std::string::const_iterator> parseCommant(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
		static const boost::regex re(R"(^\s*((?<line>\/\/)|(?<range>\/\*)))");
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

	// 符号なし数値解析
	static optional<pair<std::string::const_iterator, unsigned long long>> parseUInt(const std::string::const_iterator& begin, const std::string::const_iterator& end) {
		if (auto r = regexSearch(begin, end, boost::regex(R"(^\s*([0-9]+))"))) {
			const auto t = std::make_pair(
				(*r)[0].second,
				boost::lexical_cast<unsigned long long>((*r)[1]));
			return t;
		}
		return std::nullopt;
	}

	// 関数解析
	struct ParsedFunction {
		using PairIterator = pair<std::string::const_iterator, std::string::const_iterator>;
		PairIterator						functionName;	// 関数名
		std::string::const_iterator			next;			// 次の位置
		std::map<std::string, PairIterator>	args;			// 引数リスト

		optional<std::string> findArgString(const std::string name)const {
			if (auto i = args.find(name); i != args.end()) return std::string(i->second.first, i->second.second);
			return std::nullopt;
		}

		optional<unsigned long long> findArgUInt(const std::string name)const {
			const auto i = args.find(name);
			if (i == args.end()) return std::nullopt;
			const auto r = parseUInt(i->second.first, i->second.second);
			if (!r) return std::nullopt;
			if (r->first != i->second.second) return std::nullopt;
			return r->second;
		}

	};
	static const optional<ParsedFunction> parseFunction(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd, const std::string& functionName) {
		ParsedFunction result;

		std::string::const_iterator it;
		if (auto r = regexSearch(iBegin, iEnd, boost::regex(string::format(R"(^\s*(?<name>%1%)\W)", functionName)))) {
			result.functionName = (*r)["name"];		// ヒットした関数名
			it = (*r)["name"].second;
		} else {
			return std::nullopt;		// 該当しなかった
		}

		while (auto r = Inner::parseCommant(it, iEnd)) it = *r;	// コメントを読み飛ばす

		if (auto r = regexSearch(it, iEnd, boost::regex(R"(^\s*\()"))) {		// "(" 検索
			it = (*r)[0].second;
		} else {
			return std::nullopt;		// 該当しなかった
		}

		std::vector<decltype(result.args)::value_type::second_type> nonameArgs;

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
						result.args[boost::lexical_cast<std::string>(i)] = nonameArgs[i];
					}

					result.next = (*r)[0].second;		// 次の位置
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
						result.args[currentArgName] = (*r2)["value"];
						it = (*r2)[0].second;
					} else {
						throw Exception(Exception::Code::argumentError, it, iEnd);
					}
				} else if (auto r = regexSearch(it, iEnd, boost::regex(R"(^\s*(?<value>((?![,\)])\S)*)\s*[,\)])"))) {	// 引数値（ "," ")" まで ）
					if (const auto& sm = (*r)["value"]; sm.matched) {
						result.args[currentArgName] = sm;
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
		static const auto getUtf8Char = [](const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) -> optional<std::smatch> {
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


};

const std::string MmlCompiler::Inner::sRegexLength = R"(((\!?[0-9]+)?\.*)?(\s*[\+\-]\!?[0-9]+\.*)*)";

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

	struct Port {

		size_t	position = 0;					// 現在の位置
		size_t	defaultStep = 480;				// デフォルト音長(step)
		int		octave = 4;						// 現在のオクターブ( -2 ～ 8 )
		int		velocity = 100;					// 現在のベロシティ( 0 ～ 127)
		shared_ptr<EventNote>	beforeEvent;	// 直前の音符( ^の対象)

		MmlCompiler::Port	port;
	};

	struct State {
		const std::string& mml;

		std::string::const_iterator	it;

		State(const std::string& s)
			:mml(s), it(s.cbegin())
		{
		}

		std::map<std::string, Port>	mapPort;
		std::string					currentPortName;

	}state(mml);

	// l?? デフォルト音長
	const auto fDefaultLength = [&state] {
		try {
			static const boost::regex re(R"(^\s*l\s*((\!?[0-9]+\.*)(\s*[\+\-]\!?[0-9]+\.*)*))");
			const auto r = Inner::regexSearch(state.it, state.mml.cend(), re);		// 数字以外があるなら
			if (!r) {
				return false;
			}
			if (const auto& sm = (*r)[1]; sm.matched) {
				auto& port = state.mapPort[state.currentPortName];
				auto step = Inner::parseLength(sm.first, sm.second, port.defaultStep);
				port.defaultStep = step;
				state.it = (*r)[0].second;
				return true;
			}
			assert(false);
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::lCommandError, state.it, state.mml.cend());
	};

	// o?? オクターブ
	const auto fOctave = [&state] {
		try {
			static const boost::regex re(R"(^\s*o\s*(?<value>\-?[0-9]+))");
			boost::smatch match;
			if (!boost::regex_search(state.it, state.mml.cend(), match, re, boost::match_not_dot_newline | boost::match_single_line)) {
				return false;
			}
			if (const auto& sm = match["value"]; sm.matched) {
				int octave = boost::lexical_cast<int>(sm.str());
				if (octave < -2 || octave > 8) {		// 範囲チェック
					throw Exception(Exception::Code::oCommandRangeError, state.it, state.mml.cend());
				}
				state.mapPort[state.currentPortName].octave = octave;
				state.it = match[0].second;
				return true;
			}
			assert(false);
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::oCommandError, state.it, state.mml.cend());
	};

	// t?? テンポ
	const auto fTempo = [&state] {
		try {
			static const boost::regex re(R"(^\s*t\s*(?<value>\+?[0-9]+(\.[0-9]*)?([eE][\+-]?[0-9]+)?))");
			boost::smatch match;
			if (!boost::regex_search(state.it, state.mml.cend(), match, re, boost::match_not_dot_newline | boost::match_single_line)) {
				return false;
			}
			if (const auto& sm = match["value"]; sm.matched) {
				double tempo = boost::lexical_cast<double>(sm.str());

				auto& port = state.mapPort[state.currentPortName];
				auto e = make_shared<EventTempo>();
				e->position = port.position;
				e->tempo = tempo;
				port.port.eventList.insert(e);

				state.it = match[0].second;
				return true;
			}
			assert(false);
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::tCommandRangeError, state.it, state.mml.cend());
	};

	// @?? プログラムチェンジ
	const auto fProgramChange = [&state] {
		try {
			static const boost::regex re(R"(^\s*@\s*(?<value>[0-9]+))");
			boost::smatch match;
			if (!boost::regex_search(state.it, state.mml.cend(), match, re, boost::match_not_dot_newline | boost::match_single_line)) {
				return false;
			}
			if (const auto& sm = match["value"]; sm.matched) {
				int programNo = boost::lexical_cast<int>(sm.str());

				auto& port = state.mapPort[state.currentPortName];
				auto e = make_shared<EventProgramChange>();
				e->position = port.position;
				e->programNo = programNo;
				port.port.eventList.insert(e);

				state.it = match[0].second;
				return true;
			}
			assert(false);
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::programchangeCommandError, state.it, state.mml.cend());
	};

	// r?? 休符
	const auto fRest = [&state] {
		try {
			static const boost::regex re(R"(^(\s*r\s*))" + Inner::sRegexLength);
			const auto r = Inner::regexSearch(state.it, state.mml.cend(), re);
			if (!r) {
				return false;
			}
			auto& port = state.mapPort[state.currentPortName];
			auto step = Inner::parseLength((*r)[1].second, (*r)[0].second, port.defaultStep);
			port.position += step;
			port.beforeEvent.reset();
			state.it = (*r)[0].second;
			return true;
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::rCommandRangeError, state.it, state.mml.cend());
	};

	// a～g?? 音符
	const auto fNote = [&state] {
		try {
			static const boost::regex re(R"(^\s*(?<note>[a-g][\+\-]?)\s*)" + Inner::sRegexLength);
			const auto r = Inner::regexSearch(state.it, state.mml.cend(), re);
			if (!r) {
				return false;
			}
			auto& port = state.mapPort[state.currentPortName];
			auto step = Inner::parseLength((*r)["note"].second, (*r)[0].second, port.defaultStep);
			auto e = make_shared<EventNote>();
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
				assert(false);
				throw Exception(Exception::Code::noteCommandRangeError, state.it, state.mml.cend());
			}();
			e->length = step;
			e->velocity = port.velocity;
			port.port.eventList.insert(e);

			port.position += step;
			port.beforeEvent = e;

			state.it = (*r)[0].second;
			return true;
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::noteCommandRangeError, state.it, state.mml.cend());
	};

	// < > オクターブUPDOWN
	const auto fOctaveUpDown = [&state] {
		try {
			static const boost::regex re(R"(^\s*([<>]))");
			const auto r = Inner::regexSearch(state.it, state.mml.cend(), re);
			if (!r) {
				return false;
			}
			auto& port = state.mapPort[state.currentPortName];
			if ((*r)[1].str() == "<") {	// UP
				port.octave++;
			} else {					// DOWN
				port.octave--;
			}
			if (port.octave < -2 || port.octave > 8) {	// 範囲チェック
				throw Exception(Exception::Code::octaveUpDownRangeCommandError, state.it, state.mml.cend());
			}
			state.it = (*r)[0].second;
			return true;
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::octaveUpDownCommandError, state.it, state.mml.cend());
	};

	// ^ tie (長さを付け足す)
	const auto fTie = [&state] {
		try {
			static const boost::regex re(R"(^(\s*\^\s*))" + Inner::sRegexLength);
			const auto r = Inner::regexSearch(state.it, state.mml.cend(), re);
			if (!r) {
				return false;
			}
			auto& port = state.mapPort[state.currentPortName];
			auto step = Inner::parseLength((*r)[1].second, (*r)[0].second, port.defaultStep);
			if (port.beforeEvent) {					// 直前の音符があれば
				port.beforeEvent->length += step;	// 音符の音長に足す
			}
			port.position += step;
			state.it = (*r)[0].second;
			return true;
		} catch (const Exception& e) {
			throw e;
		} catch (...) {
		}
		throw Exception(Exception::Code::tieCommandError, state.it, state.mml.cend());
	};

	try {
		while (true) {

			if (auto it = Inner::parseCommant(state.it, state.mml.cend())) {	// コメント
				state.it = *it;
				continue;
			}

			if (auto r = Inner::parseFunction(state.it, state.mml.cend(), "createPort")) {		// createPort
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::createPortPortNameError, state.it, mml.cend());
				}
				if (auto r2 = state.mapPort.insert({ name ,Port() }); !r2.second) {			// port追加 
					throw Exception(Exception::Code::createPortDuplicateError, state.it, mml.cend());
				} else {
					auto& port = r2.first->second;
					const auto ch = r->findArgUInt("channel").value_or(0);
					if (ch < 1 || ch > 16) {
						throw Exception(Exception::Code::createPortChannelError, state.it, mml.cend());
					}
					port.port.channel = static_cast<decltype(port.port.channel)>(ch - 1);
				}

				state.it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(state.it, state.mml.cend(), "port")) {			// port
				const auto name = (*r).findArgString("0").value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::portNameError, state.it, mml.cend());
				}
				state.mapPort[name];			// 作成を担保
				state.currentPortName = name;
				state.it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(state.it, state.mml.cend(), "volume|V")) {		// volume|V
				const auto vol = r->findArgUInt("0").value_or(-1);
				if (vol < 0 || vol > 127) {
					throw Exception(Exception::Code::volumeError, state.it, mml.cend());
				}

				auto& port = state.mapPort[state.currentPortName];
				auto e = make_shared<EventVolume>();
				e->position = port.position;
				e->volume = static_cast<decltype(e->volume)>(vol);
				port.port.eventList.insert(e);

				state.it = r->next;
				continue;
			}

			if (auto r = Inner::parseFunction(state.it, state.mml.cend(), "pan|panpot")) {		// pan|panpot
				const auto n = r->findArgUInt("0").value_or(-1);
				if (n < 0 || n > 127) {
					throw Exception(Exception::Code::panError, state.it, mml.cend());
				}

				auto& port = state.mapPort[state.currentPortName];
				auto e = make_shared<EventPan>();
				e->position = port.position;
				e->pan = static_cast<decltype(e->pan)>(n);
				port.port.eventList.insert(e);

				state.it = r->next;
				continue;
			}

			if (fDefaultLength()) {			// l? デフォルト音長
				continue;
			}
			if (fOctave()) {				// o? オクターブ
				continue;
			}
			if (fTempo()) {					// t? テンポ
				continue;
			}
			if (fProgramChange()) {			// @? プログラムチェンジ
				continue;
			}
			if (fRest()) {					// r 休符
				continue;
			}
			if (fNote()) {					// a～g 音符
				continue;
			}
			if (fOctaveUpDown()) {			// < > オクターブUPDOWN
				continue;
			}
			if (fTie()) {					// ^ tie (長さを付け足す)
				continue;
			}

			static const boost::regex re(R"(^\s*(\S+))");
			boost::smatch match;
			if (boost::regex_search(state.it, state.mml.cend(), match, re, boost::match_not_dot_newline | boost::match_single_line)) {
				if (match[1].matched) {
					throw Exception(Exception::Code::unknownError, match[1].first, state.mml.cend());
				}
				break;
			}

			break;		// 完了
		}

		Result result;
		for (auto& i : state.mapPort) {
			result.emplace_back(std::move(i.second.port));
		}
		return result;

	} catch (const Exception& e) {
		throw e;
	} catch (const std::exception& e) {
		throw Exception(Exception::Code::stdEexceptionError, state.it, state.mml.cend(), e.what());
	} catch (...) {
		throw Exception(Exception::Code::unknownError, state.it, state.mml.cend());
	}
}
