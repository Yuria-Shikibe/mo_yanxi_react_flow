add_rules("mode.debug", "mode.release")
set_arch("x64")
set_encodings("utf-8")
set_project("mo_yanxi.react_flow")

if is_plat("windows") then
    if is_mode("debug") then
        set_runtimes("MDd")
    else
        set_runtimes("MD")
    end
else
    set_runtimes("c++_shared")
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
add_requires("gtest")

if is_plat("linux") then
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

includes("xmake2cmake.lua");
