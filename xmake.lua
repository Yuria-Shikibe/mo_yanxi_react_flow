add_rules("mode.debug", "mode.release")
set_encodings("utf-8")
set_project("mo_yanxi.react_flow")
set_version("1.0")

-- print(get_config("toolchain"))

option("add_test")
    set_default(false)
    set_description("Add google test target")
option_end()

option("use_libcxx")
    set_default(true)
    set_description("Use libc++")
option_end()

if not get_config("runtimes") then
    if is_plat("windows") then
        if is_mode("debug") then
            set_runtimes("MDd")
        else
            set_runtimes("MD")
        end
    else
        set_runtimes("c++_shared")
    end
end

local pkg_name = "mo_yanxi.utility";

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

if has_config("add_test") then
    add_requires("gtest")
end

if (not get_config("toolchain") == "msvc") and has_config("use_libcxx") then
    add_requireconfs("*", {configs = {cxflags = "-stdlib=libc++", ldflags = {"-stdlib=libc++", "-lc++abi", "-lunwind"}}})
    add_cxflags("-stdlib=libc++")
    add_ldflags("-stdlib=libc++", "-lc++abi", "-lunwind")
end


target("mo_yanxi.react_flow")
    set_kind("object")
    set_languages("c++23")
    set_policy("build.c++.modules", true)

    add_packages(pkg_name, {public = true})
    add_files("src/**.ixx", {public = true})

    -- used for cmakelist generation
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

    set_warnings("all")
    set_warnings("pedantic")
target_end()

if has_config("add_test") then

target("mo_yanxi.react_flow.test")
    set_kind("binary")
    set_languages("c++23")
    set_policy("build.c++.modules", true)

    set_warnings("all")
    set_warnings("pedantic")

    add_deps("mo_yanxi.react_flow", {public = true})
    add_packages("gtest")
    add_files("test/**.cpp")
target_end()

end


target("mo_yanxi.react_flow.example")
    set_kind("binary")
    set_languages("c++23")
    set_policy("build.c++.modules", true)
    set_warnings("all")
    set_warnings("pedantic")

    add_deps("mo_yanxi.react_flow", {public = true})
    add_files("examples/**.cpp")
    set_default(false)
target_end()

includes("xmake2cmake.lua");
