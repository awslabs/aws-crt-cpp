import Builder


class BuildCrt(Builder.Action):
    """
    Custom build step exists so that the CMake args can be tweaked programmatically.
    It was proving impossible to tweak them exactly how we wanted via the existing
    technique of overlaying data from json files.
    """

    def run(self, env):
        project = Builder.Project.find_project('aws-crt-cpp')
        return Builder.CMakeBuild(project, args_transformer=self._transform_cmake_args)

    def _transform_cmake_args(self, env, project, cmake_args):
        new_args = []

        # remove or swap out flags that we don't want for C++
        for arg in cmake_args:
            # we want to use PERFORM_HEADER_CHECK_CXX instead of PERFORM_HEADER_CHECK
            # if arg == "-DPERFORM_HEADER_CHECK=ON":
            #     arg = "-DPERFORM_HEADER_CHECK_CXX=ON"
            new_args.append(arg)

        return new_args
