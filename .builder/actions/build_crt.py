import Builder
import sys


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
        is_using_openssl = False

        # remove or swap out flags that we don't want for C++
        for arg in cmake_args:
            # we want to use PERFORM_HEADER_CHECK_CXX instead of PERFORM_HEADER_CHECK
            if arg == "-DPERFORM_HEADER_CHECK=ON":
                arg = "-DPERFORM_HEADER_CHECK_CXX=ON"
            if (arg == "-DUSE_OPENSSL=ON"):
                is_using_openssl = True

            new_args.append(arg)

        # If we are using OpenSSL on Linux, then we want to make sure our CI container has the latest version
        # for that platform, therefore we need to update
        if (is_using_openssl):
            print(
                "Trying to install a SSL development library (either libssl-dev or openssl-devel)")
            # Make sure libssl-dev is installed
            try:
                Builder.InstallPackages(['libssl-dev']).run(env)
                print("Installed/Updated libssl-dev")
            except:
                try:
                    Builder.InstallPackages(['openssl-devel']).run(env)
                    print("Installed/Updated openssl-devel")
                except:
                    print(
                        "ERROR - could not install either libssl-devel or openssl-devel. Skipping setting '-DUSE_OPENSSL=ON'")
        return new_args
