
#include <bits/stdc++.h>

#include <boost/program_options.hpp>

#include "./stringformat/StringFormat.h"
#include "./sequencer/SmfToMml.h"

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
			std::cout << "rlib-MML smftomml version 1.1.1" << std::endl;
			return 0;
		}

		if (vm.count("help")) {
			std::cout << desc << std::endl;		// ヘルプ表示
			return 0;
		}

		po::notify(vm);

		auto fs = [&] {
			auto path = std::filesystem::u8path(input);
			std::fstream fs(path, std::ios::in | std::ios::binary);
			if (fs.fail()) {
				throw std::runtime_error("input file open error.");
			}
			return fs;
		}();

		try {
			auto smf = midi::Smf::fromStream(fs);
			const std::string mml = sequencer::smfToMml(smf);

			auto path = std::filesystem::u8path(output);
			std::fstream fs(path, std::ios::out | std::ios::binary | std::ios::trunc);
			if (fs.fail()) {
				throw std::runtime_error("output file open error.");
			}
			fs.write(mml.data(), mml.size());

		} catch (const std::exception& e) {
			const auto s = e.what();
			throw std::runtime_error(s);
		}
	} catch (std::exception& e) {
		std::cerr << e.what() << std::endl;
		return 1;
	}

	return 0;
}
