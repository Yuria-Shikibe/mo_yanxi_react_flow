add_rules("mode.debug", "mode.release")
set_encodings("utf-8")
set_project("mo_yanxi.react_flow")
set_version("0.1.0")

option("spec_mo_yanxi_utility_path")
    set_description("External repository path (checks for xmake.lua)")
    set_default("")
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

option("add_examples")
    set_default(false)
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
    local util_xmake = path.join(util_path, "xmake.lua")
    if os.isfile(util_xmake) then
        includes(util_xmake)
        has_path_spec = true
    else
        wprint("spec_mo_yanxi_utility_path does not contain xmake.lua: %s", util_xmake)
    end
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
    set_languages("c++23")
    add_rules("project.common")
    add_files("src/**.ixx", {public = true})

    if has_path_spec then
        add_deps(pkg_name, {public = true})
    else
        add_packages(pkg_name, {public = true})
    end

target_end()


-- 测试 Target
target("mo_yanxi.react_flow.test")
    set_kind("binary")
    set_languages("c++23")

    set_enabled(has_config("add_test"))
    set_default(has_config("add_test"))

    add_deps("mo_yanxi.react_flow", {public = true})
    add_packages("gtest")
    add_files("test/**.cpp")
target_end()

-- 示例 Target
target("mo_yanxi.react_flow.example")
    set_kind("binary")
    set_languages("c++23")

    add_deps("mo_yanxi.react_flow", {public = true})
    --
    set_enabled(has_config("add_examples"))
    set_default(has_config("add_examples"))

    add_files("examples/**.cpp")
    add_files("examples/**.ixx")
target_end()

-- 基准测试 Target
target("mo_yanxi.react_flow.benchmark")
    set_kind("binary")
    set_languages("c++23")

    add_rules("project.optimized")
    add_deps("mo_yanxi.react_flow", {public = true})

    set_enabled(has_config("add_benchmark"))
    set_default(has_config("add_benchmark"))

    add_packages("benchmark")
    add_files("benchmark/**.cpp")
target_end()
