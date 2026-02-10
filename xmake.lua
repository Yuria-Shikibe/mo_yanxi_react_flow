add_rules("mode.debug", "mode.release")
set_encodings("utf-8")
set_project("mo_yanxi.react_flow")
set_version("1.0")

option("spec_mo_yanxi_utility_path")
    set_description("External repository path (checks for xmake.lua)")
    on_check(function (option)
        import("core.base.option")
        local p = os.getenv("MO_YANXI_UTILITY_PATH")
        if p and #p > 0 then
            local config_file = p
            if os.isdir(p) then config_file = path.join(p, "xmake.lua") end
            if os.isfile(config_file) then
                option:set_value(path.normalize(config_file))
            else
                option:set_value(nil)
                utils.warning("Invalid ext_repo_path: %s (xmake.lua not found)", p)
            end
        end
    end)
    set_showmenu(true)
option_end()

option("add_test")
    set_default(false)
    set_description("Add google test target")
option_end()

option("add_benchmark")
    set_default(false)
    set_description("Add add_benchmark target")
option_end()

option("use_libcxx")
    add_deps("toolchain")
    on_check(function (option)
        option:enable(get_config("toolchain") == "clang" and not is_plat("windows"))
    end)
    set_description("Use libc++")
option_end()

-- Runtimes setup
if not get_config("runtimes") then
    if is_plat("windows") then
        set_runtimes(is_mode("debug") and "MDd" or "MD")
    else
        set_runtimes("c++_shared")
    end
end

-- Package dependency setup
local pkg_name = "mo_yanxi.utility"
local util_path = get_config("spec_mo_yanxi_utility_path")
local has_path_spec = false

if util_path and #util_path > 0 then
    includes(util_path)
    has_path_spec = true
end

if (not has_path_spec) then
    package(pkg_name)
        if os.isdir("../mo_yanxi_utility") then
            set_sourcedir("../mo_yanxi_utility")
        else
            add_urls("https://github.com/Yuria-Shikibe/mo_yanxi_utility.git")
        end
        set_policy("platform.longpaths", true)
        on_install(function (package)
            import("package.tools.xmake").install(package, {add_legacy = false, add_latest = false})
        end)
    package_end()
    add_requires(pkg_name)
end

if has_config("add_test") then add_requires("gtest") end
if has_config("add_benchmark") then add_requires("benchmark") end

if (get_config("toolchain") ~= "msvc") and has_config("use_libcxx") then
    add_requireconfs("*", {configs = {cxflags = "-stdlib=libc++", ldflags = {"-stdlib=libc++", "-lc++abi", "-lunwind"}}})
    add_cxflags("-stdlib=libc++")
    add_ldflags("-stdlib=libc++", "-lc++abi", "-lunwind")
end

-------------------------------------------------------------------------------
-- Rules (新增部分：统一配置规则)
-------------------------------------------------------------------------------

-- 1. 基础规则：适用于所有 Target (Lib, Test, Example, Bench)
rule("project.common")
    on_load(function (target)
        target:set("languages", "c++23")
        target:set("policy", "build.c++.modules", true)
        target:set("warnings", "all", "pedantic")
    end)
rule_end()

-- 2. 二进制规则：适用于 Test, Example, Bench
--    自动依赖主库，并设置为二进制类型
rule("project.binary")
    add_deps("project.common")
    on_load(function (target)
        target:set("kind", "binary")
        target:add("deps", "mo_yanxi.react_flow", {public = true})
    end)
rule_end()

-- 3. 优化规则：适用于 Example, Bench
--    开启最高优化并保留调试符号
rule("project.optimized")
    on_load(function (target)
        target:set("optimize", "fastest")
        target:set("symbols", "debug")
        target:set("strip", "debug")
    end)
rule_end()

-------------------------------------------------------------------------------
-- Targets
-------------------------------------------------------------------------------

-- 主库 Target
target("mo_yanxi.react_flow")
    set_kind("object")
    add_rules("project.common")
    add_files("src/**.ixx", {public = true})

    if has_path_spec then
        add_deps(pkg_name, {public = true})
    else
        add_packages(pkg_name, {public = true})

        if is_mode("cmake_gen") then
            on_load(function (target)
                local pkg = target:pkg(pkg_name)
                if pkg then
                    local pkg_dir = pkg:installdir()
                    local ixx_pattern = path.join(pkg_dir, "modules", "**.ixx")
                    target:add("files", ixx_pattern, {public = true})
                    print("Adding module files: " .. ixx_pattern)
                end
            end)
        end
    end

target_end()

-- 测试 Target
target("mo_yanxi.react_flow.test")
    add_rules("project.binary") -- 自动继承 common 和 main lib 依赖

    set_enabled(has_config("add_test"))

    add_packages("gtest")
    add_files("test/**.cpp")
target_end()

-- 示例 Target
target("mo_yanxi.react_flow.example")
    add_rules("project.binary", "project.optimized")

    set_default(false)

    add_files("examples/**.cpp")
    add_files("examples/**.ixx")
target_end()

-- 基准测试 Target
target("mo_yanxi.react_flow.benchmark")
    add_rules("project.binary", "project.optimized")

    set_enabled(has_config("add_benchmark"))
    set_default(false)

    add_packages("benchmark")
    add_files("benchmark/**.cpp")
target_end()

includes("xmake2cmake.lua")