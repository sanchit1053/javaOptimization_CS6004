/*******************************************************************************
 * Copyright (c) 2017, 2021 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

Boolean FAIL = false
String SRC_REPO = 'https://github.com/${ghprbGhRepository}.git'
def BAD_FILES = []
String HASHES = '###################################'

timeout(time: 6, unit: 'HOURS') {
    stage('Copyright Check') {
        node ('worker') {
            timestamps {
                checkout changelog: false, poll: false,
                        scm: [$class: 'GitSCM',
                            branches: [[name: sha1]],
                            doGenerateSubmoduleConfigurations: false,
                            extensions: [[$class: 'CloneOption',
                                            depth: 0,
                                            noTags: false,
                                            reference: '/home/jenkins/openjdk_cache',
                                            shallow: false]],
                            userRemoteConfigs: [[refspec: "+refs/pull/${ghprbPullId}/*:refs/remotes/origin/pr/${ghprbPullId}/* +refs/heads/${ghprbTargetBranch}:refs/remotes/origin/${ghprbTargetBranch}",
                                                    url: SRC_REPO]]]
                FILES = sh (
                    script: "git diff -C --diff-filter=ACM --name-only origin/${ghprbTargetBranch} HEAD",
                    returnStdout: true
                ).trim()
                echo FILES
                if (FILES == "") {
                    echo "There are no files to check for copyrights"
                } else {
                    def FILES_LIST = FILES.split("\\r?\\n")
                    DATE_YEAR = sh (
                        script: "date +%Y",
                        returnStdout: true
                    ).trim()

                    // Set a different Copyright regex depending on the Repo the PR is from
                    REGEX = "\'Copyright \\(c\\) ([0-9]{4}), ${DATE_YEAR}\'"

                    IGNORE_LIST = []
                    if (fileExists("${WORKSPACE}/.copyrightignore")) {
                        IGNORE_LIST = readFile '.copyrightignore'
                        IGNORE_LIST = IGNORE_LIST.tokenize("\n")
                    }

                    FILES_LIST.each() {
                        println "Checking file: '${it}'"
                        if (IGNORE_LIST.contains(it)) {
                            echo "Ignoring file"
                        } else {
                            RESULT = sh (
                                script: "grep -qE ${REGEX} '${it}'",
                                returnStatus: true)
                            if (RESULT != 0) {
                                echo "FAILURE - Copyright date in file: '${it}' appears to be incorrect"
                                FAIL = true
                                BAD_FILES << "${it}"
                            } else {
                                echo "Copyright date in file: appears to be correct"
                            }
                        }
                    }
                    if (FAIL) {
                        echo "${HASHES}"
                        echo "The following files were modified and have incorrect copyrights"
                        BAD_FILES.each() {
                            echo "${it}"
                        }
                        echo "${HASHES}"
                        error("One or more files do not have valid copyrights.")
                    } else {
                        echo "All modified files appear to have correct copyrights"
                    }
                }
            }
        }
    }
}
