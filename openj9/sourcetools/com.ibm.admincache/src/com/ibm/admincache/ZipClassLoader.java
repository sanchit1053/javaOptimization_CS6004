/*******************************************************************************
 * Copyright (c) 2008, 2017 IBM Corp. and others
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

package com.ibm.admincache;

import java.util.jar.*;
import java.io.*;

class ZipClassLoader extends ClassLoader {
	private JarInputStream _zis;
	private JarEntry _ze;
	
	public ZipClassLoader(JarInputStream zis) {
		super(ClassLoader.getSystemClassLoader());
		_zis = zis;
	}
	
	public void setZipEntry(JarEntry ze) {
		_ze = ze;
	}
	
	protected Class findClass(String name) throws ClassNotFoundException {
		if (_ze == null)
			throw new ClassNotFoundException();
		if (_ze.getName().equals(name))
			throw new ClassNotFoundException();
		byte[] classBuffer = new byte[1024];
		ByteArrayOutputStream baos = new ByteArrayOutputStream();
		try {
			int bytesRead = 0;
			while ((bytesRead = _zis.read(classBuffer)) > 0) {
				baos.write(classBuffer, 0, bytesRead);
			};
		} catch (Exception e) {
			throw new ClassNotFoundException();
		}
		
		Class clazz=null;
		try {
			byte[] bytes = baos.toByteArray();
			clazz = defineClass(name, bytes, 0, bytes.length);
		}
		catch (ClassFormatError e) {
			return null;
		}
		return clazz;
	}
	
	public Class loadClass(String name) {
		Class clazz=null;
		try {
			clazz = super.loadClass(name,true);
		}
		catch (VerifyError e) {
			return null;
		}
		catch (Exception e) {
			try {
				clazz = findClass(name);
			} catch (Exception e2) {
				return null;
			}
		}
			
		return clazz;
	}

}
