
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_WARN_DEPRECATED
#include "external/nob/nob.h"

#define BUILD_FOLDER "build/"
#define SRC_FOLDER "src/"

int main(int argc, char** argv)
{
	NOB_GO_REBUILD_URSELF(argc, argv);

	Cmd cmd = {0};

	if (!mkdir_if_not_exists(BUILD_FOLDER))
		return 1;

	// List all your source files here
	const char* src_files[] = {
	    SRC_FOLDER "main.c",
	    SRC_FOLDER "ext.c",
	    SRC_FOLDER "initialise.c",
	    SRC_FOLDER "helpers.c",
	    "external/SPIRV-Reflect/spirv_reflect.c"};

	// Compile into one final binary
	nob_cc(&cmd);
	nob_cc_flags(&cmd);

	// extra compile/link flags (similar to build.sh)
	cmd_append(&cmd, "-D_DEBUG", "-DVK_USE_PLATFORM_WAYLAND_KHR");
	cmd_append(&cmd, "-lvulkan", "-lm", "-lglfw", "-lpthread", "-ldl", "-std=c99");

	nob_cc_output(&cmd, BUILD_FOLDER "tri");

	for (size_t i = 0; i < ARRAY_LEN(src_files); ++i)
	{
		nob_cc_inputs(&cmd, src_files[i]);
	}

	if (!nob_cmd_run(&cmd))
		return 1;

	nob_log(NOB_INFO, "Build complete → %stri", BUILD_FOLDER);

	return 0;
}
