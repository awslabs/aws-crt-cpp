#!/usr/bin/env bash

set -ex

# Redirect output to stderr.
exec 1>&2

GITHUB_TOKEN=$1

pushd $(dirname $0) > /dev/null

version=$(git describe --tags --abbrev=0)
sed --in-place -r -e "s/set\\(AWS_CRT_CPP_VERSION \".+\"\\)/set(AWS_CRT_CPP_VERSION \"${version}\")/" CMakeLists.txt
echo "Updating AWS_CRT_CPP_VERSION default to ${version}"

if git diff --exit-code CMakeLists.txt > /dev/null; then
    echo "No version change"
else
    git config --local user.email "aws-sdk-common-runtime@amazon.com"
    git config --local user.name "GitHub Actions"
    git add CMakeLists.txt
    git commit -m "Updated version to ${version}"
    git tag -f ${version}
    git push "https://${GITHUB_ACTOR}:${GITHUB_TOKEN}@github.com/awslabs/aws-crt-cpp.git" --force --tags
fi

popd > /dev/null
