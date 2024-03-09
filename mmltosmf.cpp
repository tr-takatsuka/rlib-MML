
#include <bits/stdc++.h>

#include <boost/program_options.hpp>

#include "./stringformat/StringFormat.h"
#include "./sequencer/MmlToSmf.h"

using namespace rlib;

int main(const int argc, const char* const argv[])
{
	namespace po = boost::program_options;

	try {
		std::string input, output;
		po::options_description desc("options");
		desc.add_options()
			("version", "show version")
			("help", "show help")
			("input,i", po::value(&input)->required(), "input file (required)")		// 入力ファイルパス(mml)
			("output,o", po::value(&output)->required(), "output file (required)")	// 出力ファイル(mid)
			;

		po::positional_options_description pd;
		// pd.add("input", -1);

		po::variables_map vm;
		po::store(po::command_line_parser(argc, argv).options(desc).positional(pd).run(), vm);

		if (vm.count("version")) {
			std::cout << "rlib-MML mmltosmf version 1.2.0" << std::endl;
			return 0;
		}

		if (vm.count("help")) {
			std::cout << desc << std::endl;		// ヘルプ表示
			return 0;
		}

		po::notify(vm);

		const std::string mml = [&] {
			auto path = std::filesystem::u8path(input);
			std::fstream fs(path, std::ios::in | std::ios::binary);
			if (fs.fail()) {
				throw std::runtime_error("input file open error.");
			}
			const std::istreambuf_iterator<char> begin(fs);
			return std::string(begin, std::istreambuf_iterator<char>());
		}();

		try {
			const midi::Smf smf = sequencer::mmlToSmf(mml);
			auto fileImage = smf.getFileImage();

			auto path = std::filesystem::u8path(output);
			std::fstream fs(path, std::ios::out | std::ios::binary | std::ios::trunc);
			if (fs.fail()) {
				throw std::runtime_error("output file open error.");
			}
			fs.write(reinterpret_cast<const char* const>(fileImage.data()), fileImage.size());

		} catch (const sequencer::MmlCompiler::Exception& e) {

			const size_t lineNumber = [&] {	// 行番号取得
				static const std::regex re(R"(\r\n|\n|\r)");
				size_t lf = 0;
				for (auto i = std::sregex_token_iterator(mml.cbegin(), e.it, re, -1); i != std::sregex_token_iterator(); i++) lf++;
				return lf;
			}();
			const auto msg = sequencer::MmlCompiler::Exception::getMessage(e.code);	// エラーメッセージ取得
			auto errorWord = [&] {
				std::string s(e.errorWord);
				std::smatch m;
				if (std::regex_search(s, m, std::regex(R"(\r\n|\n|\r)"))) {
					s = std::string(s.cbegin(), m[0].first);
				}
				return s;
			}();

			const auto s = string::format(R"(line %d %s : %s)", lineNumber, errorWord, msg);
			throw std::runtime_error(s);
		}

	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}
