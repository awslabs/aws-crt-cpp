import Builder
import json
import os
import re
import subprocess
import sys


class CrtCiPrep(Builder.Action):

    def run(self, env):
        env.shell.setenv("AWS_TESTING_COGNITO_IDENTITY", env.shell.get_secret("aws-c-auth-testing/cognito-identity"))
        env.shell.setenv("AWS_TESTING_STS_ROLE_ARN", env.shell.get_secret("aws-c-auth-testing/sts-role-arn"))

        env.shell.setenv("AWS_TEST_MQTT5_DIRECT_MQTT_HOST", "localhost")
        env.shell.setenv("AWS_TEST_MQTT5_DIRECT_MQTT_PORT", "1883")
        env.shell.setenv("AWS_TEST_MQTT5_PROXY_HOST", "localhost")
        env.shell.setenv("AWS_TEST_MQTT5_PROXY_PORT", "1883")

        actions = []

        return Builder.Script(actions, name='crt-ci-prep')
