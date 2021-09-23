#!/usr/bin/env bash

set -ex

# Redirect output to stderr.
exec 1>&2

GITHUB_TOKEN=$1
[ -n "$GITHUB_TOKEN" ]

pushd $(dirname $0) > /dev/null

version=$(git tag --sort=-creatordate | head -n1)
sed --in-place -r -e "s/set\\(AWS_CRT_CPP_VERSION \".+\"\\)/set(AWS_CRT_CPP_VERSION \"${version}\")/" CMakeLists.txt
echo "Updating AWS_CRT_CPP_VERSION default to ${version}"

if git diff --exit-code CMakeLists.txt > /dev/null; then
    echo "No version change"
else
    git config --local user.email "aws-sdk-common-runtime@amazon.com"
    git config --local user.name "GitHub Actions"
    git add CMakeLists.txt
    git commit -m "Updated version to ${version}"

    # awkward - we need to snip the old tag message and then force overwrite the tag with the new commit but
    # preserving the old message
    tag_message=$(git tag -l -n20 ${version} | sed -n "s/${version} \(.*\)/\1/p")
    echo "Old tag message is: ${tag_message}"

    # delete the old tag on github
    #git push "https://${GITHUB_ACTOR}:${GITHUB_TOKEN}@github.com/awslabs/aws-crt-cpp.git" :refs/tags/${version}

    # create new tag on latest commit with old message
    git tag -f ${version} -m "${tag_message}"

    # push new tag and commit to github
    git push "https://${GITHUB_ACTOR}:${GITHUB_TOKEN}@github.com/awslabs/aws-crt-cpp.git" --force --tags
fi

popd > /dev/null
