#!/usr/bin/env bash

set -ex

# Redirect output to stderr.
exec 1>&2

pushd $(dirname $0) > /dev/null

version=$(git describe --tags --abbrev=0)
sed --in-place -e "s/set(AWS_CRT_CPP_VERSION \"v1.0.0-dev\")/set(AWS_CRT_CPP_VERSION \"${version}\")/" CMakeLists.txt
echo "Updating AWS_CRT_CPP_VERSION default to ${version}"

git diff --exit-code CMakeLists.txt > /dev/null
if [ $? -eq 1 ]; then
    git add CMakeLists.txt
    git commit -m "Updated version to ${version}"
    git tag -f ${version}
    git push --follow-tags
fi

popd > /dev/null
