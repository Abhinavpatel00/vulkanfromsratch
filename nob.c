
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_WARN_DEPRECATED
#include "external/nob/nob.h"

#define BUILD_FOLDER "build/"
#define SRC_FOLDER "src/"
#define SHADERS_DIR "shaders"
#define SPV_DIR "compiledshaders"

static bool has_shader_ext(const char *name) {
	const char *dot = strrchr(name, '.');
	if (!dot) return false;
	return strcmp(dot, ".vert") == 0 || strcmp(dot, ".frag") == 0 || strcmp(dot, ".comp") == 0 ||
		   strcmp(dot, ".geom") == 0 || strcmp(dot, ".tesc") == 0 || strcmp(dot, ".tese") == 0;
}

static bool compile_shaders_in_dir(const char *dir) {
	Nob_File_Paths children = {0};
	if (!nob_read_entire_dir(dir, &children)) return false;
	bool ok = true;
	for (size_t i = 0; i < children.count; ++i) {
		const char *name = children.items[i];
		if (!name || name[0] == '.') continue; // skip hidden and dot entries
		size_t mark = nob_temp_save();
		const char *path = nob_temp_sprintf("%s/%s", dir, name);
		Nob_File_Type t = nob_get_file_type(path);
		if (t == NOB_FILE_DIRECTORY) {
			if (!compile_shaders_in_dir(path)) ok = false;
		} else if (t == NOB_FILE_REGULAR && has_shader_ext(name)) {
			const char *base = nob_path_name(path);
			const char *out = nob_temp_sprintf(SPV_DIR "/%s.spv", base);
			Cmd glslc = {0};
			cmd_append(&glslc, "glslc", path, "-o", out);
			nob_log(NOB_INFO, "glslc %s -> %s", path, out);
			if (!cmd_run(&glslc)) { ok = false; }
		}
		nob_temp_rewind(mark);
	}
	nob_da_free(children);
	return ok;
}

int main(int argc, char** argv)
{
	NOB_GO_REBUILD_URSELF(argc, argv);

	Cmd cmd = {0};

	if (!mkdir_if_not_exists(BUILD_FOLDER))
		return 1;

	// Cross-platform shader compilation using nob APIs
	if (!mkdir_if_not_exists(SPV_DIR)) return 1;
	{
		Cmd probe = {0};
		cmd_append(&probe, "glslc", "--version");
		if (!cmd_run(&probe)) {
			nob_log(NOB_WARNING, "glslc not found; skipping shader compilation");
		} else {
			if (!compile_shaders_in_dir(SHADERS_DIR)) return 1;
		}
	}

	// Split C and C++ sources
	const char* c_src_files[] = {
		SRC_FOLDER "main.c",
		SRC_FOLDER "ext.c",
		SRC_FOLDER "initialise.c",
		SRC_FOLDER "helpers.c",
		"external/SPIRV-Reflect/spirv_reflect.c"
	};
	const char* cpp_src_files[] = {
		SRC_FOLDER "vma.cpp"
	};

	// Build folder
	const char* output = BUILD_FOLDER "tri";

	// Compile C sources to objects
	for (size_t i = 0; i < ARRAY_LEN(c_src_files); ++i) {
		Cmd c = {0};
		cmd_append(&c, "gcc", "-c", c_src_files[i], "-o");
		char obj_path[512];
		const char *path = c_src_files[i];
		const char *slash = strrchr(path, '/');
		const char *name = slash ? slash + 1 : path;
		const char *dot = strrchr(name, '.');
		size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
		snprintf(obj_path, sizeof(obj_path), BUILD_FOLDER "%.*s.o", (int)base_len, name);
	cmd_append(&c, obj_path);
	cmd_append(&c, "-D_DEBUG", "-DVK_USE_PLATFORM_WAYLAND_KHR", "-std=c99", "-IVulkanMemoryAllocator/include");
		if (!cmd_run(&c)) return 1;
	}

	// Compile C++ sources to objects
	for (size_t i = 0; i < ARRAY_LEN(cpp_src_files); ++i) {
		Cmd cxx = {0};
		cmd_append(&cxx, "g++", "-c", cpp_src_files[i], "-o");
		char obj_path[512];
		const char *path = cpp_src_files[i];
		const char *slash = strrchr(path, '/');
		const char *name = slash ? slash + 1 : path;
		const char *dot = strrchr(name, '.');
		size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
		snprintf(obj_path, sizeof(obj_path), BUILD_FOLDER "%.*s.o", (int)base_len, name);
	cmd_append(&cxx, obj_path);
	cmd_append(&cxx, "-D_DEBUG", "-DVK_USE_PLATFORM_WAYLAND_KHR", "-std=c++17", "-IVulkanMemoryAllocator/include");
		if (!cmd_run(&cxx)) return 1;
	}

	// Link all objects with g++
	Cmd link = {0};
	cmd_append(&link, "g++", "-o", output);
	// Add all object files from build folder (simple enumeration of known ones)
	cmd_append(&link, BUILD_FOLDER "main.o", BUILD_FOLDER "ext.o", BUILD_FOLDER "initialise.o", BUILD_FOLDER "helpers.o", BUILD_FOLDER "spirv_reflect.o", BUILD_FOLDER "vma.o");
	cmd_append(&link, "-lvulkan", "-lm", "-lglfw", "-lpthread", "-ldl");
	if (!cmd_run(&link)) return 1;

	nob_log(NOB_INFO, "Build complete → %stri", BUILD_FOLDER);

	return 0;
}
