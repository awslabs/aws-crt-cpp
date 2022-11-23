import Builder
import json
import os
import re
import subprocess
import sys


class CrtCiPrep(Builder.Action):

    def run(self, env):
        # Setup environment for mqtt5 test
        mqtt5_setup = subprocess.call("mqtt5_test_setup.sh")
        if mqtt5_setup != 0:
            print("Warning: Failed to setup mqtt5 testing environment parameters. The mqtt5 tests might be failed. ")

        env.shell.setenv("AWS_TESTING_COGNITO_IDENTITY", env.shell.get_secret("aws-c-auth-testing/cognito-identity"))
        env.shell.setenv("AWS_TESTING_STS_ROLE_ARN", env.shell.get_secret("aws-c-auth-testing/sts-role-arn"))

        actions = []

        return Builder.Script(actions, name='crt-ci-prep')
