<!--
(c) Copyright IBM Corp. 2017, 2018 All Rights Reserved

This code is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 only, as
published by the Free Software Foundation.

IBM designates this particular file as subject to the "Classpath" exception
as provided by IBM in the LICENSE file that accompanied this code.

This code is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
version 2 for more details (a copy is included in the LICENSE file that
accompanied this code).

You should have received a copy of the GNU General Public License version
2 along with this work; if not, see <http://www.gnu.org/licenses/>.
-->

# Eclipse OpenJ9 Build README

## How to Build Eclipse OpenJ9

1. For details of how to build Eclipse OpenJ9 see https://www.eclipse.org/openj9/oj9_build.html


## How to use our analysis

1. Build the system using `make all`
2. compile your file using `javac -Xint`
3. run the file using the `java` binary in `build/linux-x86_64-normal-server-release/images/j2re-image/bin/java`
4. store the result in a file, say `output.txt`
5. run the `./findStatistics.sh <className> output.txt`
6. enjoy
