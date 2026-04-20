#pragma once

#include <string>

#include <Windows.h>

struct RuntimeOptions
{
private:
	RuntimeOptions() :
		Quiet(false),
		UseStaticCodeAnalysis(false),
		ExportFormat(Native),
		Attach(false),
		AttachPid(0),
		ConsolidateAuxiliary(false)
	{}

	std::string sourcePath;

public:
	static RuntimeOptions& Instance()
	{
		static RuntimeOptions instance;
		return instance;
	}

	bool Quiet;
	bool UseStaticCodeAnalysis;

	enum ExportFormatType
	{
		Native,
		NativeV2,
		Cobertura,
		Clover
	} ExportFormat;


	std::string OutputFile;

	std::string MergedOutput;
	std::string WorkingDirectory;
	std::string CodePath;
	std::string Executable;
	std::string ExecutableArguments;
	std::string PackageName = "Program.exe";
	std::string SolutionPath;

	// When Attach is true, the coverage runner will attach (DebugActiveProcess)
	// to an already-running process identified by AttachPid instead of launching
	// a new process with CreateProcess. This is primarily used internally when
	// a mismatched-bitness child process is spawned by the main target (e.g.
	// vstest.console.exe (x64) spawning testhost.x86.exe (x86)); the parent
	// Coverage-x64.exe then launches Coverage-x86.exe with -attach <pid> to
	// instrument the x86 child.
	bool Attach;
	DWORD AttachPid;

	// When true, after the coverage run finishes and any sibling-bitness
	// CPPCoverage helper processes have produced their own .cov files, the
	// master process merges those files into its own -o output file and
	// deletes them. Only supported for Native / NativeV2 export formats
	// (the same restriction that applies to -m / MergedOutput).
	bool ConsolidateAuxiliary;

	std::string SourcePath()
	{
		if (sourcePath.empty() && !CodePath.empty())
		{
			auto idx = CodePath.find("x64");
			if (idx == std::string::npos)
			{
				idx = CodePath.find("Debug");
			}
			if (idx == std::string::npos)
			{
				idx = CodePath.find("Release");
			}
			if (idx == std::string::npos)
			{
				idx = CodePath.find('\\');
			}
			if (idx == std::string::npos)
			{
				throw "Cannot locate source file base for this executable";
			}
			sourcePath = CodePath.substr(0, idx);
		}
		return sourcePath;
	}
};