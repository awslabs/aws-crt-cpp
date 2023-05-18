import Builder


class BuildCrt(Builder.Action):
    """
    Custom build step exists so that the CMake args can be tweaked programmatically.
    It was proving impossible to tweak them exactly how we wanted via the existing
    technique of overlaying data from json files.
    """

    def run(self, env):
        project = Builder.Project.find_project('aws-crt-cpp')
        return Builder.CMakeBuild(project)
