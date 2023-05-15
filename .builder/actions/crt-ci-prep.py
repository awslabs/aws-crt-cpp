import Builder
import json
import os
import re
import subprocess
import sys
import tempfile


class CrtCiPrep(Builder.Action):
    def _write_environment_script_secret_to_env(self, env, secret_name):
        mqtt5_ci_environment_script = env.shell.get_secret(secret_name)
        env_line = re.compile('^export\s+(\w+)=(.+)')

        lines = mqtt5_ci_environment_script.splitlines()
        for line in lines:
            env_pair_match = env_line.match(line)
            if env_pair_match.group(1) and env_pair_match.group(2):
                env.shell.setenv(env_pair_match.group(1), env_pair_match.group(2), quiet=True)

    def _write_secret_to_temp_file(self, env, secret_name):
        secret_value = env.shell.get_secret(secret_name)

        fd, filename = tempfile.mkstemp()
        os.write(fd, str.encode(secret_value))
        os.close(fd)

        return filename

    def _write_s3_to_temp_file(self, env, s3_file):
        try:
            tmp_file = tempfile.NamedTemporaryFile(delete=False)
            tmp_file.flush()
            tmp_s3_filepath = tmp_file.name
            cmd = ['aws', '--region', 'us-east-1', 's3', 'cp',
                    s3_file, tmp_s3_filepath]
            env.shell.exec(*cmd, check=True, quiet=True)
            return tmp_s3_filepath
        except:
            print (f"ERROR: Could not get S3 file from URL {s3_file}!")
            raise RuntimeError("Could not get S3 file from URL")


    def run(self, env):
        env.shell.setenv("AWS_TESTING_COGNITO_IDENTITY", env.shell.get_secret("aws-c-auth-testing/cognito-identity"))
        env.shell.setenv("AWS_TESTING_STS_ROLE_ARN", env.shell.get_secret("aws-c-auth-testing/sts-role-arn"))

        # Unfortunately, we can't use NamedTemporaryFile and a with-block because NamedTemporaryFile is not readable
        # on Windows.
        self._write_environment_script_secret_to_env(env, "mqtt5-testing/github-ci-environment")

        cert_file_name = self._write_secret_to_temp_file(env, "unit-test/certificate")
        key_file_name = self._write_secret_to_temp_file(env, "unit-test/privatekey")

        env.shell.setenv("AWS_TEST_MQTT5_IOT_CORE_CERTIFICATE_PATH", cert_file_name, quiet=True)
        env.shell.setenv("AWS_TEST_MQTT5_IOT_CORE_KEY_PATH", key_file_name, quiet=True)

        # PKCS12 setup (MacOS only)
        if (sys.platform == "darwin"):
            pkcs12_file_name = self._write_s3_to_temp_file(env, "s3://aws-crt-test-stuff/unit-test-key-pkcs12.pem")
            env.shell.setenv("AWS_TEST_MQTT311_IOT_CORE_PKCS12_KEY", pkcs12_file_name)
            env.shell.setenv("AWS_TEST_MQTT311_IOT_CORE_PKCS12_KEY_PASSWORD", "PKCS12_KEY_PASSWORD")

        actions = []

        return Builder.Script(actions, name='crt-ci-prep')
