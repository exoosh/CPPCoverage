#pragma once

#include "base64.h"
#include "FileCallbackInfo.h"
#include "MergeRunner.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>

namespace TestFormat
{
    class TestNativeV2;
}

class MergeRunnerV2 : public MergeRunner
{
public:
    friend class TestFormat::TestNativeV2;
private:
    // Each entry holds both the case-preserved path (what we write back to
    // disk) and the accumulated coverage. The dictionary is keyed by a
    // lowercased version of the path so that two .cov files that disagree on
    // casing (e.g. a cross-bitness master/sibling pair, or a single runner
    // whose PDB reported mixed casings) collapse into one entry instead of
    // producing duplicates that downstream consumers reject.
    struct CoverageEntry
    {
        std::string writePath;
        FileCoverageV2 coverage;
    };
    using DictCoverage = std::unordered_map<std::string, CoverageEntry>;

    static std::string normalizePathKey(const std::string& path)
    {
        std::string result(path.size(), '\0');
        for (size_t i = 0; i < path.size(); ++i)
        {
            const char c = path[i];
            result[i] = (c == '/') ? '\\'
                : (char)std::tolower((unsigned char)c);
        }
        return result;
    }

    std::string clean(const std::string& content) const
    {
		// Need to remove return line (not supported by regex)
		const std::regex PatternClean("\r|\n|\t", std::regex_constants::ECMAScript);
		return  std::regex_replace(content, PatternClean, "");
    }

    DictCoverage makeDictionary(const std::string& filename) const
	{
        std::ifstream outputFile(filename.c_str(), std::fstream::in);
        if (!outputFile.is_open())
        {
            const std::string msg = "Merge failure: Impossible to open file: " + filename;
            throw std::exception(msg.c_str());
        }

        // Read file in one shot (not giga file)
		const auto size = std::filesystem::file_size(filename);
		std::string content(size, '\0');
		std::ifstream in(filename);
		in.read(content.data(), size);

		// Need to remove return line (not supported by regex)
		auto contentClean = clean(content);
		content.clear();
        
        return createDictionnary(filename, contentClean);
    }

    DictCoverage createDictionnary(const std::string& filename, const std::string& contentClean) const
    {
        DictCoverage dictOutput;
		try
		{
			// Parse each regexp 
			std::regex Pattern(R"(<file path=\"([^\"]*)\" md5=\"(\w{32})\"><stats nbLinesInFile=\"(\d*)\" nbLinesOfCode=\"(\d*)\" nbLinesCovered=\"(\d*)\"\/><coverage>([^<]*)<)",
				std::regex_constants::ECMAScript | std::regex_constants::icase);

			const std::vector<std::smatch> matches{
				std::sregex_iterator{contentClean.begin(), contentClean.end(), Pattern},
				std::sregex_iterator{}
			};

			for (const auto& match : matches)
			{
				if (match.ready())
				{
					try
					{
						const auto entryPath = match.str(1);

						std::string values;
						Base64::Decode(match.str(6), values);
						assert(values.size() % 2 == 0);

						FileCoverageV2 profile(values.size() / sizeof(FileCoverageV2::LineArray::value_type));
						profile._nbLinesFile = std::stoi(match.str(3));
						profile._nbLinesCode = std::stoi(match.str(4));
						profile._nbLinesCovered = std::stoi(match.str(5));

						profile.md5Code = match.str(2);
						std::memcpy(profile._code.data(), values.data(), values.size());

						const auto key = normalizePathKey(entryPath);
						auto existing = dictOutput.find(key);
						if (existing == dictOutput.end())
						{
							dictOutput.emplace(key, CoverageEntry{ entryPath, std::move(profile) });
						}
						else if (!existing->second.coverage.merge(profile))
						{
							// Different line counts for the same (case-insensitive)
							// source path: keep whichever one we saw first, since
							// we can't sensibly reconcile them.
							std::cerr << "Merge warning: case-variant of "
								<< existing->second.writePath
								<< " has mismatched size; dropping " << entryPath << std::endl;
						}
					}
					catch (const std::runtime_error& e)
					{
						std::cerr << "Bad data into " << filename << " with error: " << e.what() << std::endl;
					}
				}
			}
		}
		catch (const std::regex_error& e)
		{
			std::cerr << "Bad regexp: " << e.what() << std::endl;
		}
        return dictOutput;
    }

    void merge(const DictCoverage& dictOutput, DictCoverage& dictMerge)
    {
        for (const auto& kv : dictOutput)
        {
            auto itMerge = dictMerge.find(kv.first);
            if (itMerge != dictMerge.end())
            {
                if (!itMerge->second.coverage.merge(kv.second.coverage))
                {
                    std::cerr << "Merge warning: impossible to merge "
                        << itMerge->second.writePath
                        << ": size between src/dst is not same." << std::endl;
                }
            }
            else
            {
                dictMerge.emplace(kv.first, kv.second);
            }
        }
    }

public:
    /// Constructor
    /// \param[in] opts: application option. Need MergedOutput and OutputFile valid and defined + ExportFormat MUST BE Native.
    MergeRunnerV2(const RuntimeOptions& opts):
    MergeRunner(opts)
    {
        assert(_options.ExportFormat == RuntimeOptions::NativeV2); // Support only this !
    }

    /// Run merge
    void execute() override
    {
        std::filesystem::path outputPath(_options.OutputFile);
        std::filesystem::path mergedPath(_options.MergedOutput);
        
        // Check we have data
        if(!std::filesystem::exists(outputPath))
        {
            const std::string msg = "Merge failure: Impossible to find output file: " + _options.OutputFile;
            throw std::exception(msg.c_str());
        }

        // Nothing to merge = Copy and quit
        if (!std::filesystem::exists(mergedPath))
        {
            std::filesystem::copy(outputPath, mergedPath);
            return;
        }

        // ---- Make merge ---------------------------------------------------------------
        // Step 1: Parse output files and define a dictionary
        DictCoverage dictOutput = makeDictionary(_options.OutputFile);
        DictCoverage dictMerge  = makeDictionary(_options.MergedOutput);

        // Step 2: Parse merge
        merge(dictOutput, dictMerge);

        // Step 3: Write dictionary (on empty file)
		std::ofstream ofs(_options.MergedOutput);

        FileCoverageV2::writeHeader(ofs);

        for (const auto& cover : dictMerge)
        {
            cover.second.coverage.write(cover.second.writePath, ofs);
        }

        FileCoverageV2::writeFooter(ofs);
        
        ofs.close();
    }
};