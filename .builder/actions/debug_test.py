import Builder
import os
import sys

def _project_dirs(env, project):
    if not project.resolved():
        raise Exception('Project is not resolved: {}'.format(project.name))

    source_dir = project.path
    build_dir = os.path.join(env.build_dir, project.name)
    install_dir = env.install_dir

    # cross compiles are effectively chrooted to the source_dir, normal builds need absolute paths
    # or cmake gets lost because it wants directories relative to source
    if env.toolchain.cross_compile:
        # all dirs used should be relative to env.source_dir, as this is where the cross
        # compilation will be mounting to do its work
        source_dir = str(Path(source_dir).relative_to(env.root_dir))
        build_dir = str(Path(build_dir).relative_to(env.root_dir))
        install_dir = str(Path(install_dir).relative_to(env.root_dir))

    return source_dir, build_dir, install_dir


class DebugTest(Builder.Action):
    def run(self, env):
        sh = env.shell
        toolchain = env.toolchain

        project = Builder.Project.find_project('aws-crt-cpp')
        project_source_dir, project_build_dir, project_install_dir = _project_dirs(env, project)

        ctest = toolchain.ctest_binary()
        sh.exec(*toolchain.shell_env, ctest, "--version")
        sh.exec(*toolchain.shell_env, ctest, "--debug", working_dir=project_build_dir, check=True)
        # Try to generate the coverage report. Will be ignored by ctest if no coverage data available.
        sh.exec(*toolchain.shell_env, ctest, "-T", "coverage", working_dir=project_build_dir, check=True)
