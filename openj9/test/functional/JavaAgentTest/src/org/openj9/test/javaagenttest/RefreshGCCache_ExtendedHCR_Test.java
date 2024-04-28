/*******************************************************************************
 * Copyright (c) 2001, 2018 IBM Corp. and others
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
package org.openj9.test.javaagenttest;

import org.testng.annotations.Test;
import org.testng.log4testng.Logger;
import org.testng.Assert;
import org.testng.AssertJUnit;
import java.lang.instrument.IllegalClassFormatException;
import org.openj9.test.javaagenttest.util.Transformer;

@Test(groups = { "level.sanity", "level.extended" })
public class RefreshGCCache_ExtendedHCR_Test {

	private static final Logger logger = Logger.getLogger(RefreshGCCache_ExtendedHCR_Test.class);

	public static final String OPTION_EXTENDED_HCR = "EXTENDED-HCR";

	/**
	 * In this test we instrument java/lang/ClassLoader class and add a new method
	 * */
	@Test
	public void test_EXTENDED_HCR() {
		logger.info("Starting RefreshGCCache_ExtendedHCR_Test...");
		Transformer transformer = new Transformer( Transformer.ADD_NEW_METHOD );
		byte[] transformedClassBytes;

		try {
			logger.debug( "Attempting BCI..." );
			transformedClassBytes = transformer.transform( null, "java/lang/ClassLoader", ClassLoader.class, null, null );
			logger.warn("transformedClassBytes is " + transformedClassBytes);
			Assert.assertNotNull(transformedClassBytes, "transformedClassBytes is null");

			logger.debug("BCI complete");
			logger.debug("Attempting to redefine instrumented classs with new method added...");
			AgentMain.redefineClass(ClassLoader.class, transformedClassBytes);
			logger.debug( "Retransformation complete." );

			logger.debug("Forcing GC...");
			System.gc(); //If GC special classes cache is not refreshed, JVM will crash at this point.

			logger.debug("Attempting to use instrumented method ClassLoader#getResource()");
			RefreshGCCache_ExtendedHCR_Test.class.getClassLoader().getResource("");

		} catch (IllegalClassFormatException e) {
			Assert.fail("Unexpected exception occured " + e.getMessage(), e);
		}
	}
}


