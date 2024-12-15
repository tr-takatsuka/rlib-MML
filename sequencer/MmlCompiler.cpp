
//#ifndef _MSC_VER
//#include <bits/stdc++.h>
//#endif
#include <regex>
#include <charconv>
#include <variant>
#include <optional>
#include <iostream>

#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>

#include "../json/Json.h"

#include "./MmlCompiler.h"
#include "./MidiEvent.h"

using namespace rlib;
using namespace rlib::sequencer;

std::string MmlCompiler::Exception::getMessage(Code code) {
	static const std::map<Code, std::string> m = {
		{Code::lengthError,						u8R"(音長の指定に誤りがあります)"},
		{Code::lengthMinusError,				u8R"(音長を負値にはできません)"},
		{Code::commentError,					u8R"(コメント指定に誤りがあります)"},
		{Code::argumentError,					u8R"(関数の引数指定に誤りがあります)"},
		{Code::argumentUnknownError,			u8R"(関数に不明な引数名があります)"},
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
		{Code::createPortError,					u8R"(CreatePort コマンドに誤りがあります)"},
		{Code::createPortPortNameError,			u8R"(CreatePort コマンドのポート名指定に誤りがあります)"},
		{Code::createPortDuplicateError,		u8R"(CreatePort コマンドでポート名が重複しています)" },
		{Code::createPortChannelError,			u8R"(CreatePort コマンドのチャンネル指定に誤りがあります)" },
		{Code::portError,						u8R"(Port コマンドに誤りがあります)" },
		{Code::portNameError,					u8R"(Port コマンドのポート名指定に誤りがあります)" },
		{Code::volumeError,						u8R"(Volume コマンドの指定に誤りがあります)" },
		{Code::volumeRangeError,				u8R"(Volume コマンドの値が範囲外です)" },
		{Code::panError,						u8R"(Pan コマンドの指定に誤りがあります)" },
		{Code::panRangeError,					u8R"(Pan コマンドの値が範囲外です)" },
		{Code::pitchBendError,					u8R"(PitchBend コマンドの指定に誤りがあります)" },
		{Code::pitchBendRangeError,				u8R"(PitchBend コマンドの値が範囲外です)" },
		{Code::controlChangeError,				u8R"(ControlChange コマンドの指定に誤りがあります)" },
		{Code::controlChangeRangeError,			u8R"(ControlChange コマンドの値が範囲外です)" },
		{Code::createSequenceError,				u8R"(CreateSequence コマンドに誤りがあります)" },
		{Code::createSequenceDuplicateError,	u8R"(CreateSequence コマンドで名前が重複しています)" },
		{Code::createSequenceNameError,			u8R"(CreateSequence コマンドの名前指定に誤りがあります)" },
		{Code::sequenceError,					u8R"(Sequence コマンドに誤りがあります)" },
		{Code::sequenceNameError,				u8R"(Sequence コマンドの名前指定に誤りがあります)" },
		{Code::metaError,						u8R"(Meta コマンドに誤りがあります)" },
		{Code::metaTypeError,					u8R"(Meta コマンドの type の指定に誤りがあります)" },
		{Code::fineTuneError,					u8R"(FineTune コマンドの指定に誤りがあります)" },
		{Code::fineTuneRangeError,				u8R"(FineTune コマンドの値が範囲外です)" },
		{Code::coarseTuneError,					u8R"(CoarseTune コマンドの指定に誤りがあります)" },
		{Code::coarseTuneRangeError,			u8R"(CoarseTune コマンドの値が範囲外です)" },
		{Code::definePresetFMError,				u8R"(DefinePresetFM コマンドに誤りがあります)" },
		{Code::definePresetFMNoError,			u8R"(DefinePresetFM コマンドのプログラムナンバー指定に誤りがあります)" },
		{Code::definePresetFMRangeError,		u8R"(DefinePresetFM コマンドの値が範囲外です)" },
		{Code::unknownError,					u8R"(解析出来ない書式です)"},
		{Code::stdEexceptionError,				u8R"(std::excption エラーです)"},
	};
	if (auto i = m.find(code); i != m.end()) {
		return i->second;
	}
	assert(false);
	return "unknown";
}

namespace {

using regex = boost::regex;

std::optional<boost::smatch> regexSearch(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd, const regex& re) {
	// match_not_dot_newline  : . は改行以外にマッチさせる。(std::regexと同じにする)
	// match_single_line	  : ^ は先頭行の行頭のみにマッチ
	// boost::match_continuous: 先頭から始まる部分シーケンスにのみマッチすることを指定する
	boost::smatch m;
	if (boost::regex_search(iBegin, iEnd, m, re, boost::match_not_dot_newline | boost::match_single_line | boost::match_continuous)) {
		return m;
	}
	return std::nullopt;
}

using PairIterator = std::pair<std::string::const_iterator, std::string::const_iterator>;

// std::string::const_iteratorをポインタに変換 (TODO: std::to_address に差し替えたい)
template <typename T> const std::pair<const char*, const char*> toAddress(const T& iBegin, const T& iEnd) {
	const size_t size = std::distance(iBegin, iEnd);//iEnd - iBegin;
	if (size == 0) return { nullptr,nullptr };
	auto p = &(*iBegin);
	return { p,p + size };
}

// 文字列の前方一致比較
template <typename T> std::optional<T> isStartsWith(const T& iBegin, const T& iEnd, const std::string& s) {
	if (static_cast<size_t>(iEnd - iBegin) >= s.size() && std::equal(s.cbegin(), s.cend(), iBegin)) {
		return iBegin + s.size();
	}
	return std::nullopt;
};

// 文字列リテラルパース
auto parseString(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
	struct Result {
		std::string::const_iterator	next;					// 次の位置
		std::optional<PairIterator>	parsedString;			// パースした文字列(nullの場合はパースエラー)
	};
	if (iBegin == iEnd) return std::optional<Result>();
	std::optional<Result> result({ iBegin });
	if (*iBegin == '"') {									// "・・・" 形式の文字列
		auto i = std::find(iBegin + 1, iEnd, '"');				// 終端を検索
		if (i == iEnd) return result;							// 終端が見つからないならパースエラー
		result->parsedString.emplace(iBegin + 1, i);
		result->next = i + 1;									// " の次
		return result;
	}
	if (*iBegin == 'R') {									// R"xx(・・・)xx" 形式の文字列
		static const regex re(u8R"(^R\"(\w*)\()");
		const auto m = regexSearch(iBegin, iEnd, re);
		if (!m) return std::optional<Result>();					// 対象外
		const std::string delimiter = (*m)[1];
		const auto iString = (*m)[0].second;					// 文字列開始位置
#if 1
		const auto sEnd = ")" + delimiter + "\"";
		auto i = std::search(iString, iEnd, sEnd.begin(), sEnd.end());	// 終端を検索
		if (i == iEnd) return result;									// 終端が見つからないならパースエラー
		result->parsedString = { iString,i };
		result->next = i + sEnd.size();							// 文字列開始位置
#else
		const regex reEnd(u8R"(^([\s\S]*?)\))" + delimiter + u8R"(")");
		const auto m2 = regexSearch(iString, iEnd, reEnd);		// 終端を検索
		if (!m2) return result;									// 終端が見つからないならパースエラー
		result->parsedString = (*m2)[1];
		result->next = (*m2)[0].second;							// 文字列開始位置
#endif
		return result;
	}
	return std::optional<Result>();							// 対象外
};

// コメント解析（コメントと空白と改行を読み飛ばす）
std::string::const_iterator parseComment(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
	auto it = iBegin;
	while (true) {
		it = std::find_if(it, iEnd, [](const auto& ch) {return !std::isspace(ch); });	// 先頭の空白を読み飛ばす
		static const std::initializer_list<std::pair<std::string, regex>> list = {
			{"//",regex(R"(^.*(\r\n|\n|\r|$))")},
			{"/*",regex(R"(^((?!\*\/)[\s\S])*\*\/)") },
		};
		auto i = list.begin();
		for (; i != list.end(); i++) {
			if (const auto r = isStartsWith(it, iEnd, i->first)) {
				if (const auto s = regexSearch(*r, iEnd, i->second)) {	// コメントの終了位置検索
					it = (*s)[0].second;
					break;
				} else {
					throw MmlCompiler::Exception(MmlCompiler::Exception::Code::commentError, it, iEnd);	// 見つからないならエラー
				}
			}
		}
		if (i == list.end()) break;	// コメントではないなら抜ける
	}
	return it;
};

// 整数パース
auto parseInt(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
	struct {
		std::string::const_iterator			next;
		std::variant<intmax_t, uintmax_t>	value;	// 正数:uintmax_t 負数:intmax_t +○○:intmax_t
	}result;
	std::string::const_iterator it = iBegin;

	// 16進数
	if (auto r = isStartsWith(it, iEnd, "0x")) {
		it = *r;
		const auto [pBegin, pEnd] = toAddress(it, iEnd);
		uintmax_t value;
		const auto [ptr, ec] = std::from_chars(pBegin, pEnd, value, 16);
		if (ec != std::errc{}) return std::optional<decltype(result)>();
		result.value = value;
		result.next = iBegin + (ptr - &(*iBegin));
		return std::optional(result);
	}

	if (it == iEnd) return std::optional<decltype(result)>();
	const int sign = [&] {
		if (*it == '-') {
			it++;
			return -1;
		}
		if (*it == '+') {
			it++;
			return +1;
		}
		return 0;
	}();
	const auto [pBegin, pEnd] = toAddress(it, iEnd);
	uintmax_t value;
	const auto [ptr, ec] = std::from_chars(pBegin, pEnd, value, 10);
	if (ec != std::errc{}) return std::optional<decltype(result)>();
	if (sign < 0) {
		result.value = -static_cast<intmax_t>(value);
	} else if (sign > 0) {
		result.value = static_cast<intmax_t>(value);
	} else {
		result.value = static_cast<uintmax_t>(value);
	}
	result.next = iBegin + (ptr - &(*iBegin));
	return std::optional(result);
}

// 浮動小数点数パース
auto parseDouble(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {
	struct {
		std::string::const_iterator next;
		double						value;
	}result;
#if 0
	// emscripten では std::from_chars の double 版が使えない "call to deleted function 'from_chars'"
	const auto [begin, end] = toAddress(iBegin, iEnd);
	const auto [ptr, ec] = std::from_chars(begin, end, result.value, std::chars_format::fixed);	// 指数表記は不許可
	if (ec != std::errc{}) return std::optional<decltype(result)>();
	result.next = iBegin + (ptr - begin);
	return std::optional(result);
#else
	if (const auto m = regexSearch(iBegin, iEnd, regex(R"(^([0-9]+(\.[0-9]*)?))"))) {	// regex(R"(^(\+?[0-9]+(\.[0-9]*)?([eE][\+-]?[0-9]+)?))"
		result.value = boost::lexical_cast<double>((*m)[1].str());
		result.next = (*m)[0].second;
		return std::optional(result);
	}
	return std::optional<decltype(result)>();
#endif
}


// 関数解析
struct ParseFunctionResult {
	PairIterator									functionName;		// 関数名
	std::string::const_iterator						next;				// 次の位置
	std::vector<MmlCompiler::Util::Word>			argsList;			// 引数リスト(名前ナシ連番)
	std::map<std::string, MmlCompiler::Util::Word>	argsName;			// 引数リスト(名前アリ)

	template <typename T> std::optional<T> findArg(const std::string& name)const {
		if (auto i = argsName.find(name); i != argsName.end() && std::holds_alternative<T>(i->second)) {
			return std::get<T>(i->second);
		}
		return std::nullopt;
	}
	template <typename T> std::optional<T> findArg(size_t index)const {
		if (argsList.size() > index && std::holds_alternative<T>(argsList[index])) {
			return std::get<T>(argsList[index]);
		}
		return std::nullopt;
	}

	template <typename T> std::optional<std::string> findArgString(const T& name)const {
		if (auto r = findArg<PairIterator>(name)) {
			return std::string(r->first, r->second);
		}
		return std::nullopt;
	}

	template <typename T> std::optional<std::intmax_t> findArgInt(const T& name)const {
		if (auto r = findArg<std::intmax_t>(name)) {
			return r;
		}
		if (auto r = findArg<std::uintmax_t>(name)) {
			return r;
		}
		return std::nullopt;
	}

	// 整数でも浮動小数でも有効とする
	template <typename T> std::optional<double> findArgNumber(const T& name)const {
		if (auto r = findArg<std::intmax_t>(name)) {
			return r;
		}
		if (auto r = findArg<std::uintmax_t>(name)) {
			return r;
		}
		if (auto r = findArg<double>(name)) {
			return r;
		}
		return std::nullopt;
	}


};

auto parseFunction(
	const std::string::const_iterator& iBegin,
	const std::string::const_iterator& iEnd,
	const std::vector<std::string>& functionNames,				// 関数名
	const std::set<std::string>& argNames,						// 名前付き引数名(ココにない引数名はエラー)
	const size_t argCount = (std::numeric_limits<size_t>::max)()	// 名前ナシ引数数(ココを超える数の引数はエラー)
) {
	std::optional<ParseFunctionResult> result = ParseFunctionResult();

	const auto func = [&]()->std::optional<std::string::const_iterator> {
		struct R {
			PairIterator				functionName;	// 関数名
			std::string::const_iterator	next;			// 次の位置
		};
		for (auto& name : functionNames) {
			const auto r = isStartsWith(iBegin, iEnd, name);
			if (!r) continue;
			auto it = parseComment(*r, iEnd);		// コメントを読み飛ばす
			if (it == iEnd || *it != '(') continue;
			it++;
			result->functionName = { iBegin,*r };
			return it;
		}
		return std::nullopt;
	}();
	if (!func) return decltype(result)();		// 該当しなかった

	std::string currentArgName;
	union {									// 次トークン情報
		struct {
			uint16_t	rightParen : 1;		// )
			uint16_t	comma : 1;			// ,
			uint16_t	argName : 1;		// 引数名
			uint16_t	argValue : 1;		// 引数値
		};
		uint16_t		all = 0;
	}flags;
	flags.rightParen = true;
	flags.argName = true;
	flags.argValue = true;

	auto it = *func;
	while (true) {
		it = parseComment(it, iEnd);		// コメントを読み飛ばす
		if (it == iEnd) break;

		if (flags.rightParen && *it == ')') {		// )
			result->next = it + 1;						// 次の位置
			return result;								// 正常終了
		}

		if (flags.comma && *it == ',') {			// ,
			it++;
			flags.all = 0;
			flags.rightParen = true;
			flags.argName = true;
			flags.argValue = true;
			continue;
		}

		if (flags.argName) {		// 引数名
			static const auto re = regex(R"(^([a-zA-Z]\w*))");
			if (const auto r = regexSearch(it, iEnd, re)) {
				const auto next = parseComment((*r)[0].second, iEnd);			// コメントを読み飛ばす
				if (*next == ':') {													// ":" があれば引数名で確定
					currentArgName = (*r)[0].str();
					if (auto i = argNames.find(currentArgName); i == argNames.end()) {	// 引数名チェック
						throw MmlCompiler::Exception(MmlCompiler::Exception::Code::argumentUnknownError, it, iEnd);
					}
					it = next + 1;
					flags.all = 0;
					flags.argValue = true;
					continue;
				}
			}
		}

		if (flags.argValue) {		// 引数値

			if (auto r = MmlCompiler::Util::parseWord(it, iEnd)) {
				if (!r->word) {			// パースエラー
					throw MmlCompiler::Exception(MmlCompiler::Exception::Code::argumentError, it, iEnd);	// 文字列パースエラーとする
				}
				it = r->next;
				if (currentArgName.empty()) {
					result->argsList.emplace_back(std::move(*r->word));
					if (result->argsList.size() > argCount) {
						throw MmlCompiler::Exception(MmlCompiler::Exception::Code::argumentError, it, iEnd);	// 引数が多すぎる
					}
				} else {
					result->argsName[currentArgName] = std::move(*r->word);
					currentArgName.clear();
				}
				flags.all = 0;
				flags.rightParen = true;
				flags.comma = true;
				continue;
			}

			throw MmlCompiler::Exception(MmlCompiler::Exception::Code::argumentError, it, iEnd);
		}

		break;		// どれにも該当しないならエラー
	}
	throw MmlCompiler::Exception(MmlCompiler::Exception::Code::functionCallError, it, iEnd);
};

// 音長解析
auto parseLength(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd, size_t defaultLength) {
	struct Result {
		std::string::const_iterator	next;	// 次の位置
		size_t	step = 0;
	}result = { iBegin };
	std::string::const_iterator& it = result.next;

	intmax_t step = 0;
	bool plus = true;
	while (true) {
		it = parseComment(it, iEnd);		// コメントを読み飛ばす

		// ステップ数指定か?
		const bool stepNotation = [&] {
			if (const auto r = isStartsWith(it, iEnd, "!")) {
				it = *r;
				return true;
			}
			return false;
		}();

		intmax_t tmpStep = defaultLength;

		// 数値
		if (auto r = regexSearch(it, iEnd, regex(R"(^([0-9]+))"))) {
			size_t n = boost::lexical_cast<size_t>((*r)[1].str());
			if (stepNotation) {
				tmpStep = n;
			} else {
				tmpStep = (MmlCompiler::timeBase * 4) / n;
			}
			it = parseComment((*r)[0].second, iEnd);
		} else {
			if (stepNotation) {		// ステップ数指定しておきながら数値がないなら
				throw MmlCompiler::Exception(MmlCompiler::Exception::Code::lengthError, it, iEnd);
			}
		}

		// 付点音符
		if (auto r = regexSearch(it, iEnd, regex(R"(^(\.)+)"))) {
			const size_t n = std::count((*r)[0].first, (*r)[0].second, '.');
			size_t t = tmpStep / 2;
			for (size_t j = 0; j < n; j++, t /= 2) {
				tmpStep += t;
			}
			it = parseComment((*r)[0].second, iEnd);		// コメントを読み飛ばす
		}

		step += tmpStep * (plus ? 1 : -1);

		// + -
		if (const auto r = isStartsWith(it, iEnd, "-")) {
			it = *r;
			plus = false;
		} else if (const auto r = isStartsWith(it, iEnd, "+")) {
			it = *r;
			plus = true;
		} else {
			break;
		}
	}

	if (step < 0) {		// 負値はエラー
		throw MmlCompiler::Exception(MmlCompiler::Exception::Code::lengthMinusError, iBegin, iEnd);
	}
	result.step = step;
	return result;
}


}

class MmlCompiler::Inner
{
public:
	struct Sequence {
		std::string			name;
		std::vector<Port>	ports;
		size_t				lastPosition = 0;
	};
	template <typename T> struct LessName {
		typedef void is_transparent;
		bool operator()(const T& a, const T& b)				const { return a->name < b->name; }
		bool operator()(const std::string &a, const T& b)	const { return a < b->name; }
		bool operator()(const T& a, const std::string &b)	const { return a->name < b; }
	};
	using Sequences = std::set<std::shared_ptr<const Sequence>, LessName<std::shared_ptr<const Sequence>>>;

	static std::vector<Port> mmlToSequence(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd,const Sequences &sequences) {
		struct PortInfo {
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
		struct {
			std::map<std::string, PortInfo>	mapPort;
			PortInfo* currentPort = nullptr;
			Sequences						sequences;
			struct PastedSequence {
				size_t							position = 0;
				std::shared_ptr<const Sequence>	sequence;
				std::optional<size_t>			length;
			};
			std::list<PastedSequence>	pastedSequences;
		}state;
		state.currentPort = &state.mapPort[""];

		using iterator = std::string::const_iterator;

		// ^ tie (長さを付け足す)
		const auto parseTie = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "^")) {
				auto it = parseComment(*r, end);			// コメントを読み飛ばす
				auto& port = *state.currentPort;
				const auto len = parseLength(it, end, port.defaultStep);
				if (port.beforeEvent) {						// 直前の音符があれば
					port.beforeEvent->length += len.step;	// 音符の音長に足す
				}
				if (!port.noteUnmove || !port.beforeEvent) {
					port.position += len.step;
				}
				return len.next;
			}
			return std::nullopt;
		};

		// < > オクターブUPDOWN
		const auto parseOctaveUpDown = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			auto& port = *state.currentPort;
			const auto check = [&] {
				if (port.octave < -2 || port.octave > 8) {	// 範囲チェック
					throw Exception(Exception::Code::octaveUpDownRangeCommandError, begin, end);
				}
			};
			if (const auto r = isStartsWith(begin, end, "<")) {			// UP
				port.octave++;
				check();
				return *r;
			} else if (const auto r = isStartsWith(begin, end, ">")) {	// DOWN
				port.octave--;
				check();
				return *r;
			}
			return std::nullopt;
		};

		// ' Noteで現在位置を進めるか否かモード
		const auto parseNoteUnmove = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "'")) {
				auto& port = *state.currentPort;
				port.noteUnmove = !port.noteUnmove;		// 反転
				port.beforeEvent.reset();				// '^'の対象をクリア
				return *r;
			}
			return std::nullopt;
		};

		// @?? プログラムチェンジ
		const auto parseProgramChange = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "@")) {
				auto it = parseComment(*r, end);			// コメントを読み飛ばす
				if (const auto r2 = regexSearch(it, end, regex(R"(^([0-9]+))"))) {
					auto& port = *state.currentPort;
					auto e = std::make_shared<EventProgramChange>();
					e->position = port.position;
					e->programNo = boost::lexical_cast<int>((*r2)[1].str());
					port.port.eventList.insert(e);
					return (*r2)[0].second;
				}
				throw Exception(Exception::Code::programchangeCommandError, begin, end);
			}
			return std::nullopt;
		};

		const auto parseCreateSequence = [&state, &sequences](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "CreateSeq","CreateSequence" }, { "name","mml" })) {
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::createSequenceNameError, begin, end);
				}
				if (const auto i = state.sequences.find(name); i != state.sequences.end()) {	// 既にあるなら
					throw Exception(Exception::Code::createSequenceDuplicateError, begin, end);
				}
				const auto mml = (*r).findArg<PairIterator>("mml");
				if (!mml) {
					throw Exception(Exception::Code::createSequenceError, begin, end);
				}
				Sequence seq;
				seq.name = name;

				seq.ports = [&] {
					Sequences seq = sequences;
					for (auto s : state.sequences) seq.insert(s);		// sequencesを合成
					return mmlToSequence(mml->first, mml->second, seq);
				}();


				seq.lastPosition = [&]{
					size_t p = 0;
					for (auto& port : seq.ports) {
						if (auto i = port.eventList.rbegin(); i != port.eventList.rend()) {
							p = (std::max)(p, (*i)->position);
						}
					}
					return p;
				}();
				auto spSequence = std::make_shared<Sequence>(std::move(seq));
				state.sequences.insert(spSequence);
				return r->next;
			}
			return std::nullopt;
		};

		const auto parseSequence = [&state, &sequences](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "Seq","Sequence" }, { "length" })) {
				const auto name = (*r).findArgString(0).value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::sequenceNameError, begin, end);
				}

				decltype(state.pastedSequences)::value_type seq;
				seq.sequence = [&] {
					if (auto i = state.sequences.find(name); i != state.sequences.end()) {
						return *i;
					}
					if (auto i = sequences.find(name); i != sequences.end()) {
						return *i;
					}
					throw Exception(Exception::Code::sequenceNameError, begin, end);
				}();
				auto& port = *state.currentPort;
				seq.position = port.position;

				seq.length = [&]()->std::optional<size_t> {
					const auto length = (*r).findArg<PairIterator>("length");
					if (!length) return std::nullopt;
					const auto len = parseLength(length->first, length->second, port.defaultStep);
					if (len.next != length->second) {
						throw Exception(Exception::Code::sequenceNameError, begin, end);		// 余計な文字列がある
					}
					return len.step;
				}();

				if (!port.noteUnmove) {
					if (seq.length) {
						port.position += *seq.length;
					} else {
						const auto step = seq.sequence->lastPosition;
						const auto remainder = step % 1920;
						const auto skip = (remainder == 0) ? step + 1920 : step + (1920 - remainder);
						port.position += skip;
					}
				}

				state.pastedSequences.emplace_back(std::move(seq));

				return r->next;
			}
			return std::nullopt;
		};

		const auto parseCreatePort = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = parseFunction(begin, end, { "CreatePort","createPort" }, { "name","instrument","channel" })) {		// CreatePort
				const auto name = (*r).findArgString("name").value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::createPortPortNameError, begin, end);
				}
				auto r2 = state.mapPort.insert({ name ,PortInfo() });	// port追加
				if ( !r2.second) {				// 既にあるなら
					throw Exception(Exception::Code::createPortDuplicateError, begin, end);
				}
				auto& port = r2.first->second;
				port.port.name = name;
				if (const auto s = r->findArgString("instrument")) port.port.instrument = *s;
				const auto ch = r->findArg<uintmax_t>("channel");
				if (!ch || *ch < 1 || *ch > 16) {
					throw Exception(Exception::Code::createPortChannelError, begin, end);
				}
				port.port.channel = static_cast<decltype(port.port.channel)>(*ch - 1);
				state.currentPort = &state.mapPort[name];
				return r->next;
			}
			return std::nullopt;
		};

		const auto parsePort = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "Port","port" }, {})) {		// Port
				const auto name = (*r).findArgString(0).value_or("");
				if (name.empty()) {
					throw Exception(Exception::Code::portNameError, begin, end);
				}
				if (const auto i = state.mapPort.find(name); i == state.mapPort.end()) {	// 存在しないportならエラー
					throw Exception(Exception::Code::portNameError, begin, end);
				}
				state.currentPort = &state.mapPort[name];
				return r->next;
			}
			return std::nullopt;
		};

		const auto parseVolume = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "V","Volume","volume" }, {})) {		// Volume
				auto& port = *state.currentPort;
				if (const auto v = r->findArg<intmax_t>(0)) {	// 符号付なら相対指定
					const intmax_t n = port.volume + *v;
					port.volume = static_cast<decltype(port.volume)>(std::min<decltype(n)>(std::max<decltype(n)>(n, 0), 127));
				} else if (const auto v = r->findArg<uintmax_t>(0)) {	//  符号ナシなら絶対指定
					if (*v < 0 || *v > 127) {
						throw Exception(Exception::Code::volumeRangeError, begin, end);
					}
					port.volume = static_cast<decltype(port.volume)>(*v);
				} else {
					throw Exception(Exception::Code::volumeError, begin, end);
				}
				auto e = std::make_shared<EventVolume>();
				e->position = port.position;
				e->volume = port.volume;
				port.port.eventList.insert(e);
				return r->next;
			}
			return std::nullopt;
		};

		const auto parsePan = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "Pan","pan" }, {})) {		// Pan
				auto& port = *state.currentPort;
				if (const auto v = r->findArg<intmax_t>(0)) {	// 符号付なら相対指定
					const intmax_t n = port.pan + *v;
					port.pan = static_cast<decltype(port.pan)>(std::min<decltype(n)>(std::max<decltype(n)>(n, 0), 127));
				} else if (const auto v = r->findArg<uintmax_t>(0)) {	//  符号ナシなら絶対指定
					if (*v < 0 || *v > 127) {
						throw Exception(Exception::Code::panRangeError, begin, end);
					}
					port.pan = static_cast<decltype(port.pan)>(*v);
				} else {
					throw Exception(Exception::Code::panError, begin, end);
				}
				auto e = std::make_shared<EventPan>();
				e->position = port.position;
				e->pan = port.pan;
				port.port.eventList.insert(e);
				return r->next;
			}
			return std::nullopt;
		};

		const auto parsePitchBend = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "PitchBend","pitchBend" }, {})) {		// PitchBend
				const auto v = r->findArgInt(0);
				if (!v) throw Exception(Exception::Code::pitchBendError, begin, end);
				auto& port = *state.currentPort;
				if (*v < -8192 || *v > 8191) {
					throw Exception(Exception::Code::pitchBendRangeError, begin, end);
				}
				auto e = std::make_shared<EventPitchBend>();
				e->position = port.position;
				e->pitchBend = static_cast<decltype(e->pitchBend)>(*v);
				port.port.eventList.insert(e);
				return r->next;
			}
			return std::nullopt;
		};

		const auto parseControlChange = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "CC","ControlChange","controlChange" }, { "no","value" })) {		// ControlChange
				const auto no = [&] {
					if (auto v = r->findArg<uintmax_t>(0)) return *v;
					if (auto v = r->findArg<uintmax_t>("no")) return *v;
					throw Exception(Exception::Code::controlChangeError, begin, end);
				}();
				const auto val = [&] {
					if (auto v = r->findArg<uintmax_t>(1)) return *v;
					if (auto v = r->findArg<uintmax_t>("value")) return *v;
					throw Exception(Exception::Code::controlChangeError, begin, end);
				}();
				if (no < 0 || no > 127 || val < 0 || val > 127) {
					throw Exception(Exception::Code::controlChangeRangeError, begin, end);
				}
				auto& port = *state.currentPort;
				auto e = std::make_shared<EventControlChange>();
				e->position = port.position;
				e->no = static_cast<decltype(e->no)>(no);
				e->value = static_cast<decltype(e->value)>(val);
				port.port.eventList.insert(e);
				return r->next;
			}
			return std::nullopt;
		};
			
		// t?? テンポ
		const auto parseTempo = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "t")) {
				auto it = parseComment(*r, end);			// コメントを読み飛ばす
				if (const auto r2 = parseDouble(it, end)) {
					auto& port = *state.currentPort;
					auto e = std::make_shared<EventTempo>();
					e->position = port.position;
					e->tempo = r2->value;
					port.port.eventList.insert(e);
					return r2->next;
				}
				throw Exception(Exception::Code::tCommandRangeError, begin, end);
			}
			return std::nullopt;
		};

		// r?? 休符
		const auto parseRest = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "r")) {
				auto it = parseComment(*r, end);			// コメントを読み飛ばす
				auto& port = *state.currentPort;
				const auto len = parseLength(it, end, port.defaultStep);
				port.position += len.step;
				port.beforeEvent.reset();				// '^'の対象をクリア
				return len.next;
			}
			return std::nullopt;
		};

		// a～g?? 音符
		const auto parseNote = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			static const regex re(R"(^([a-g][\+\-]?))");
			const auto r = regexSearch(begin, end, re);
			if (!r) return std::nullopt;
			auto it = parseComment((*r)[0].second, end);			// コメントを読み飛ばす
			auto& port = *state.currentPort;
			const auto len = parseLength(it, end, port.defaultStep);
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
				if (auto i = noteTable.find((*r)[0].str()); i != noteTable.end()) {
					int note = (port.octave + 2) * 12 + i->second;
					if (note >= 0 && note <= 127) {		// 範囲チェック
						return note;
					}
				}
				throw Exception(Exception::Code::noteCommandRangeError, begin, end);
			}();
			e->length = len.step;
			e->velocity = port.velocity;
			port.port.eventList.insert(e);
			if (!port.noteUnmove) {
				port.position += len.step;
			}
			port.beforeEvent = e;
			return len.next;
		};

		// l?? デフォルト音長
		const auto parseDefaultLength = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "l")) {
				auto it = parseComment(*r, end);			// コメントを読み飛ばす
				auto& port = *state.currentPort;
				const auto len = parseLength(it, end, port.defaultStep);
				port.defaultStep = len.step;
				return len.next;
			}
			return std::nullopt;
		};

		// o?? オクターブ
		const auto parseOctave = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "o")) {
				auto it = parseComment(*r, end);			// コメントを読み飛ばす
				static const regex re(R"(^(\-?[0-9]+))");
				if (const auto r2 = regexSearch(it, end, re)) {
					auto& port = *state.currentPort;
					int octave = boost::lexical_cast<int>((*r2)[1].str());
					if (octave < -2 || octave > 8) {		// 範囲チェック
						throw Exception(Exception::Code::oCommandRangeError, begin, end);
					}
					port.octave = octave;
					port.beforeEvent.reset();				// '^'の対象をクリア
					return (*r2)[0].second;
				}
				throw Exception(Exception::Code::oCommandRangeError, begin, end);
			}
			return std::nullopt;
		};

		// v? ベロシティ
		const auto parseVelocity = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (const auto r = isStartsWith(begin, end, "v")) {
				auto it = parseComment(*r, end);			// コメントを読み飛ばす
				const auto v = parseInt(it, end);
				if (!v) throw Exception(Exception::Code::vCommandError, begin, end);
				auto& port = *state.currentPort;
				if (std::holds_alternative<intmax_t>(v->value)) {// 符号付きなら相対指定
					const intmax_t n = port.velocity + std::get<intmax_t>(v->value);
					port.velocity = static_cast<decltype(port.velocity)>(std::min<decltype(n)>(std::max<decltype(n)>(n, 0), 127));
				} else {
					assert(std::holds_alternative<uintmax_t>(v->value));
					auto n = std::get<uintmax_t>(v->value);
					if (n < 0 || n > 127) {		// 範囲チェック
						throw Exception(Exception::Code::vCommandRangeError, begin, end);
					}
					port.velocity = static_cast<decltype(port.velocity)>(n);
				}
				return v->next;
			}
			return std::nullopt;
		};

		// メタイベント
		const auto parseMeta = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "Meta" }, { "type" })) {
				const auto type = (*r).findArgInt("type");
				if (!type || *type < 0 || *type > std::numeric_limits<uint8_t>::max()) {
					throw Exception(Exception::Code::metaTypeError, begin, end);
				}
				auto& port = *state.currentPort;

				auto e = std::make_shared<EventMeta>();
				e->type = static_cast<decltype(e->type)>(*type);
				e->position = port.position;

				for (auto& arg : r->argsList) {
					if (std::holds_alternative<uintmax_t>(arg)) {
						auto n = std::get<uintmax_t>(arg);
						if (n > std::numeric_limits<uint8_t>::max()) {
							throw Exception(Exception::Code::metaTypeError, begin, end);
						}
						e->data.push_back(static_cast<uint8_t>(n));
					}else if (std::holds_alternative<intmax_t>(arg)) {
						auto n = std::get<intmax_t>(arg);
						if (n <std::numeric_limits<uint8_t>::min() || n > std::numeric_limits<uint8_t>::max()) {
							throw Exception(Exception::Code::metaTypeError, begin, end);
						}
						e->data.push_back(static_cast<int8_t>(n));
					} else if (std::holds_alternative<PairIterator>(arg)) {
						auto n = std::get<PairIterator>(arg);
						e->data.append(n.first, n.second);
					} else {
						throw Exception(Exception::Code::metaTypeError, begin, end);	// failsafe
					}
				}

				port.port.eventList.insert(e);
				return r->next;
			}
			return std::nullopt;
		};

		// FineTune
		const auto parseFineTune = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "FineTune" }, {})) {
				const auto v = r->findArgNumber(0);
				if (!v) throw Exception(Exception::Code::fineTuneError, begin, end);
				auto& port = *state.currentPort;

				constexpr double inMin = -100.0;
				constexpr double inMax = 100.0;
				if (*v < inMin || *v > inMax) {
					throw Exception(Exception::Code::fineTuneRangeError, begin, end);
				}
				constexpr int outMax = 16383;
				const auto n = static_cast<int>(std::round((*v - inMin) * outMax / (inMax - inMin)));
				const auto raw = std::clamp(n, 0, outMax);

				struct {
					midi::EventControlChange::Type no;
					uint8_t val;
				}const tbl[] = {
					{midi::EventControlChange::Type::rpnMSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::fineTune) / 0x80 & 0x7f	},
					{midi::EventControlChange::Type::rpnLSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::fineTune) & 0x7f	},
					{midi::EventControlChange::Type::dataEntryMSB,	static_cast<uint8_t>(raw / 0x80 & 0x7f)	},
					{midi::EventControlChange::Type::dataEntryLSB,	static_cast<uint8_t>(raw & 0x7f)	},
				};
				for (auto& t : tbl) {
					auto e = std::make_shared<EventControlChange>();
					e->position = port.position;
					e->no = static_cast<decltype(e->no)>(t.no);
					e->value = t.val;
					port.port.eventList.insert(e);
				}
				return r->next;
			}
			return std::nullopt;
		};

		// CoarseTune
		const auto parseCoarseTune = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "CoarseTune" }, {})) {
				const auto v = r->findArgInt(0);
				if (!v) throw Exception(Exception::Code::coarseTuneError, begin, end);
				auto& port = *state.currentPort;
				if (*v < -64 || *v > 63) {
					throw Exception(Exception::Code::coarseTuneRangeError, begin, end);
				}
				struct {
					midi::EventControlChange::Type no;
					uint8_t val;
				}const tbl[] = {
					{midi::EventControlChange::Type::rpnMSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::coarseTune) / 0x80 & 0x7f	},
					{midi::EventControlChange::Type::rpnLSB,		static_cast<uint16_t>(midi::EventControlChange::RpnType::coarseTune) & 0x7f	},
					{midi::EventControlChange::Type::dataEntryMSB,	static_cast<uint8_t>((*v + 64) & 0x7f)	},
					{midi::EventControlChange::Type::dataEntryLSB,	0	},
				};
				for (auto& t : tbl) {
					auto e = std::make_shared<EventControlChange>();
					e->position = port.position;
					e->no = static_cast<decltype(e->no)>(t.no);
					e->value = t.val;
					port.port.eventList.insert(e);
				}
				return r->next;
			}
			return std::nullopt;
			};



		// FM音色定義 (rlib-MML 固有メタイベント)
		const auto parseDefinePresetFM = [&state](const iterator& begin, const iterator& end)->std::optional<iterator> {
			if (auto r = parseFunction(begin, end, { "DefinePresetFM" }, {"no","name" }, 38)) {
				const auto no = (*r).findArgInt("no");
				if (!no || *no < 0 || *no>127) {
					throw Exception(Exception::Code::definePresetFMNoError, begin, end);
				}
				const auto name = (*r).findArgString("name").value_or("");

				static constexpr std::array maxTable = {
					//  AR  DR  SR  RR  SL  TL KS  ML DT
						31, 31, 31, 15, 15,127, 3, 15, 7,
						31, 31, 31, 15, 15,127, 3, 15, 7,
						31, 31, 31, 15, 15,127, 3, 15, 7,
						31, 31, 31, 15, 15,127, 3, 15, 7,
						7,	7,	//  AL  FB
				};
				Json::Array parameter;
				for (size_t i = 0; i < maxTable.size(); i++) {
					const auto n = (*r).findArgInt(i);
					if (!n) throw Exception(Exception::Code::definePresetFMError, begin, end);
					if (*n<0 || *n> maxTable[i] ) throw Exception(Exception::Code::definePresetFMRangeError, begin, end);
					parameter.push_back(*n);
				}

				const Json j = Json::Map{
					{"rlib-MML", Json::Map{
						{"fm4op", Json::Map{
							{std::to_string(*no), Json::Map{
								{"name", name},
								{"reg", parameter},
							}},
						}},
					}},
				};

				auto& port = *state.currentPort;
				auto e = std::make_shared<EventMeta>();
				e->type = static_cast<decltype(e->type)>(midi::EventMeta::Type::sequencerLocal);
				e->position = port.position;
				e->data = j.stringify();
				auto it = port.port.eventList.insert(e);

				return r->next;
			}
			return std::nullopt;
		};

		const std::initializer_list<
			std::pair<std::function<std::optional<iterator>(const iterator&, const iterator&)>, Exception::Code>
		> funcs = {
			{parseTie,				Exception::Code::tieCommandError},			// ^ tie (長さを付け足す)
			{parseRest,				Exception::Code::rCommandRangeError},		// r?? 休符
			{parseOctaveUpDown,		Exception::Code::octaveUpDownCommandError},	// < > オクターブUPDOWN
			{parseNoteUnmove,		Exception::Code::unknownError},				// ' noteで位置更新するか否かモード
			{parseProgramChange,	Exception::Code::programchangeCommandError},// @? プログラムチェンジ
			{parseTempo,			Exception::Code::tCommandRangeError},		// t?? テンポ
			{parseDefaultLength,	Exception::Code::lCommandError},			// l?? デフォルト音長
			{parseOctave,			Exception::Code::oCommandError},			// o?? オクターブ
			{parseCreatePort,		Exception::Code::createPortError},			// CreatePort
			{parsePort,				Exception::Code::portError},				// Port
			{parseVolume,			Exception::Code::volumeError},				// Volume
			{parsePan,				Exception::Code::panError},					// Pan
			{parsePitchBend,		Exception::Code::pitchBendError},			// PitchBend
			{parseControlChange,	Exception::Code::controlChangeError},		// ControlChange
			{parseNote,				Exception::Code::noteCommandRangeError},	// a～g?? 音符
			{parseVelocity,			Exception::Code::vCommandError},			// v? ベロシティ
			{parseCreateSequence,	Exception::Code::createSequenceError},		// CreateSequence
			{parseSequence,			Exception::Code::sequenceError},			// Sequence
			{parseMeta,				Exception::Code::metaError},				// Meta
			{parseFineTune,			Exception::Code::fineTuneError},			// FineTune
			{parseCoarseTune,		Exception::Code::coarseTuneError},			// CoarseTune
			{parseDefinePresetFM,	Exception::Code::definePresetFMError}		// DefinePresetFM
		};

		for (auto it = iBegin; true;) {
			it = parseComment(it, iEnd);			// コメントを読み飛ばす
			if (it == iEnd) break;					// 完了
			auto i = funcs.begin();
			for (; i != funcs.end(); i++) {
				try {
					if (const auto r = i->first(it, iEnd)) {
						it = (*r);
						break;
					}
				} catch (const Exception& e) {
					throw e;
				} catch (...) {
					throw Exception(i->second, it, iEnd);
				}
			}
			if(i == funcs.end()){
				throw Exception(Exception::Code::unknownError, it, iEnd);	// 未定義文字列エラー
			}
		}

		Result result;
		for (auto& i : state.mapPort) {
			result.emplace_back(std::move(i.second.port));
		}

		// Sequence 展開
		for (auto& i : state.pastedSequences) {
			size_t postion = i.position;
			auto& spSequence = i.sequence;
			const size_t length = i.length ? *i.length : (std::numeric_limits<size_t>::max)();

			for( auto& port : spSequence->ports) {
				Port p;
				p.name = port.name;
				p.instrument = port.instrument;
				p.channel = port.channel;
				for (auto& spEvent : port.eventList) {
					auto sp = spEvent->clone();
					if (sp->position >= length) {			// length より後は採用しない
						break;
					}
					sp->position += postion;
					p.eventList.insert(sp);
				}
				result.emplace_back(std::move(p));
			}
		}

		return result;

	}

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
		int count = 12;			// 必要文字数
		for (std::string::const_iterator i = iBegin; i != iEnd && count >= 0; count--) {
			auto m = getUtf8Char(i, iEnd);
			if (!m) break;
			r += (*m)[0].str();
			i = (*m)[0].second;
		}
		return r;
	}

};


MmlCompiler::Exception::Exception(Code code_, const std::string::const_iterator& itBegin, const std::string::const_iterator& itEnd, const std::string& annotate_)
	:std::runtime_error(getMessage(code_))
	, code(code_)
	, errorWord(Inner::getErrorPostionString(itBegin, itEnd))
	, it(itBegin)
	, annotate(annotate_)
{
}

MmlCompiler::Result MmlCompiler::compile(const std::string& mml) {

	MmlCompiler::Inner::Sequences sequences;
	auto r = Inner::mmlToSequence(mml.cbegin(), mml.cend(), sequences);


	return r;
}


std::optional<MmlCompiler::Util::ParsedWord> MmlCompiler::Util::parseWord(const std::string::const_iterator& iBegin, const std::string::const_iterator& iEnd) {

	ParsedWord parsedWord;

	// 文字列リテラルパース
	if (auto r = parseString(iBegin, iEnd)) {
		parsedWord.next = r->next;
		if (r->parsedString) {		// パースエラーチェック
			parsedWord.word = *r->parsedString;
		}
		return parsedWord;
	}

	// 整数パース
	if (auto r = parseInt(iBegin, iEnd)) {
		if (r->next == iEnd || *r->next != '.') {		// 直後に . があれば浮動小数点数として次へ
			parsedWord.next = r->next;
			if (std::holds_alternative<intmax_t>(r->value)) {
				parsedWord.word = std::get<intmax_t>(r->value);
			} else {
				assert(std::holds_alternative<uintmax_t>(r->value));
				parsedWord.word = std::get<uintmax_t>(r->value);
			}
			return parsedWord;
		}
	}

	// 浮動小数点数パース
	if (auto r = parseDouble(iBegin, iEnd)) {
		parsedWord.next = r->next;
		parsedWord.word = r->value;
		return parsedWord;
	}

	{// みなし文字列
		static const auto re = regex(R"(^([a-zA-Z_][\w\+\-]*))");		// 頭文字は英字_ で英字数値_+- が対象
		if (const auto r = regexSearch(iBegin, iEnd, re)) {
			const auto& m = (*r)[0];
			parsedWord.next = m.second;
			parsedWord.word = std::make_pair(m.first, m.second);
			return parsedWord;
		}
	}

	return std::nullopt;
}


void MmlCompiler::unitTest() {

	std::string s("1.5e5 is pi");
	auto a = parseDouble(s.begin(),s.end());

	{// parseInt
		std::cout << "parseInt" << std::endl;
		struct {
			std::string text;
			std::pair < size_t, std::variant<intmax_t, uintmax_t>> result;
		}static const tbl[] = {
			{"",			{0,static_cast<uintmax_t>(0)}},
			{"+",			{0,static_cast<uintmax_t>(0)}},
			{"-",			{0,static_cast<uintmax_t>(0)}},
			{"0",			{1,static_cast<uintmax_t>(0)}},
			{"-0.",			{2,static_cast<intmax_t>(0)}},
			{"1.2",			{1,static_cast<uintmax_t>(1)}},
			{"2e1.2",		{1,static_cast<uintmax_t>(2)}},
			{"+3e1.2",		{2,static_cast<intmax_t>(3)}},
			{"-4e1.2",		{2,static_cast<intmax_t>(-4)}},
			{"0x3a1.e1.2",	{5,static_cast<uintmax_t>(0x3a1)}},
			{"+0x3e1.2",	{2,static_cast<intmax_t>(0)}},
			{"0x",			{0,static_cast<intmax_t>(0)}},
			{"+x",			{0,static_cast<intmax_t>(0)}},
			{"++0",			{0,static_cast<intmax_t>(0)}},
			{"--1",			{0,static_cast<intmax_t>(0)}},
			{"+-1",			{0,static_cast<intmax_t>(0)}},
			{"-.",			{0,static_cast<intmax_t>(0)}},
		};
		for (auto &t : tbl) {
			std::cout << t.text << std::endl;
			auto r = parseInt(t.text.begin(), t.text.end());
			if (t.result.first == 0) {
				assert(!r);
			} else {
				assert(r->value == t.result.second);
				assert(t.result.first == std::distance(t.text.begin(), r->next));
			}
		}
	}


	{
		static const std::initializer_list<std::pair<std::string, size_t>> list = {
			{"",		480		},
			{"a",		480		},
			{"1",		1920	},
			{"1-!240",	1920 - 240	},
			{"1+8",		1920 + 240	},
			{".",		480 + 240	},
			{"..",		480 + 240 + 120	},
			{"..+",		480 + 240 + 120 + 480},
			{"..+a",	480 + 240 + 120 + 480},
		};
		for (auto i : list) {
			const auto& s = i.first;
			assert(parseLength(s.begin(), s.end(), 480).step == i.second);
		}
	}


	{
		static const std::initializer_list<std::pair<std::string, size_t>> list = {
			{"",		480		},
			{"a",		480		},
			{"1",		1920	},
			{"1-!240",	1920 - 240	},
			{"1+8",		1920 + 240	},
			{".",		480 + 240	},
			{"..",		480 + 240 + 120	},
			{"..+",		480 + 240 + 120	+ 480},
			{"..+a",	480 + 240 + 120 + 480},
		};
		for (auto i : list) {
			const auto &s = i.first;
			assert(parseLength(s.begin(), s.end(), 480).step == i.second);
		}
	}


}
