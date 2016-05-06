#!/bin/sh
## Copyright (c) 2016 Minoca Corp. All Rights Reserved.
##
## Script Name:
##
##     print_version.sh <file> <form> <major> <minor> <revision> <release>
##     <serial> <build_string>
##
## Abstract:
##
##     This script either prints the current version, or creates a version.h
##     header file.
##
## Author:
##
##     Evan Green 13-May-2014
##
## Environment:
##
##     Minoca (Windows) Build
##

file="$1"
form="$2"
major="$3"
minor="$4"
revision="$5"
release="$6"
serial="$7"
build_string="$8"

[ "$major" ] || major=0
[ "$minor" ] || minor=0
[ "$revision" ] || revision=0
[ "$release" ] || release=SystemReleaseDevelopment

cd $SRCROOT/os

##
## The serial number is the commit count. Use the hardcoded version in a file
## if it's supplied, or generate by counting commits in git.
##

if [ -z "$serial" ]; then
    if [ -r "$SRCROOT/os/revision" ]; then
        serial=`cat "$SRCROOT/os/revision"`

    else

        ##
        ## If this is the OS repository and it has not been git replaced with
        ## the old history, add 1000.
        ##

        os_init_rev=1598fc5f1734f7d7ee01e014ee64e131601b78a7
        serial=`git rev-list --count HEAD`
        if [ x`git rev-list --max-parents=0 HEAD` = x$os_init_rev ]; then
            serial=$(($serial + 1000))
        else
            serial=$(($serial + 1))
        fi
    fi
fi

##
## Get the build time in seconds since the Minoca epoch, which is January 1,
## 2000.
##

build_time=$((`date +%s` - 978307200))
build_time_string=`date "+%a %b %d %Y %H:%M:%S"`

##
## Generate the build string if needed.
##

if [ -z "$build_string" ]; then
    if [ -r "$SRCROOT/os/branch" ]; then
        branch=`cat $SRCROOT/os/branch`

    else
        branch=`git rev-parse --abbrev-ref HEAD`
    fi

    if [ -r "$SRCROOT/os/commit" ]; then
        commit=`cat $SRCROOT/os/commit`
    else
        commit=`git rev-parse HEAD`
    fi

    commit_abbrev=`echo $commit | cut -c1-7`
    commit8=`echo $commit | cut -c1-8`
    user="$USER"
    [ $user ] || user="$LOGNAME"
    [ $user ] || user="$USERNAME"
    [ "$user" = root ] && user=
    [ $VARIANT ] && build_string="${VARIANT}-"
    [ $user ] && build_string="${build_string}${user}-"
    [ $branch != "master" ] && build_string="${build_string}${branch}-"
    build_string="${build_string}${commit_abbrev}"
    build_string="$build_string $build_time_string"
fi

##
## For simple revisions, just print out the basic version number.
##

if [ "$form" == "simple" ]; then
    printf "$major.$minor.$revision.$serial" > $file
    exit 0
fi

if [ "$form" != "header" ]; then
    echo "Error: unknown form $form."
    exit 1
fi

comment_year=`date +%Y`
comment_date=`date +%e-%b-%Y`
file_name=`basename $file`
cat >"$file" <<_EOS
/*++

Copyright (c) $comment_year Minoca Corp. All Rights Reserved

Module Name:

    $file_name

Abstract:

    This header contains generated version information.
    This file is automatically generated.

Author:

    Minoca Build $comment_date

--*/

//
// ---------------------------------------------------------------- Definitions
//

#define VERSION_MAJOR $major
#define VERSION_MINOR $minor
#define VERSION_REVISION $revision
#define VERSION_RELEASE $release
#define VERSION_SERIAL $serial
#define VERSION_BUILD_STRING "$build_string"

#define VERSION_BUILD_TIME $build_time
#define VERSION_BUILD_TIME_STRING "$build_time_string"

//
// The full commit number string.
//

#define VERSION_COMMIT_STRING "$commit"

//
// The abbreviated commit number string.
//

#define VERSION_COMMIT_ABBREVIATED "$commit_abbrev"

//
// An integer of the first 32 bits of the commit.
//

#define VERSION_COMMIT_NUMBER 0x$commit8

//
// The current branch name.
//

#define VERSION_BRANCH "$branch"

//
// The user name of the soul doing the building.
//

#define VERSION_BUILD_USER "$user"

_EOS

