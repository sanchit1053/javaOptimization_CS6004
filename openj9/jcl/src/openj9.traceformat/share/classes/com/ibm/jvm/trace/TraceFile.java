/*[INCLUDE-IF Sidecar18-SE]*/
/*******************************************************************************
 * Copyright (c) 2007, 2016 IBM Corp. and others
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
package com.ibm.jvm.trace;

import java.util.*;
import com.ibm.jvm.format.*;

/**
 * @author srowland
 */
public class TraceFile {
	
	TraceFormat formatter = null;
	String formatterArgs[] = null;
	/**
	 * Create a TraceFile object for a binary trace file.
	 * @param traceFileNameURI a URI of the binary trace file to open and format.
	 */
	public TraceFile(String traceFileNameURI){
		formatter = new TraceFormat();
		String args[] = {traceFileNameURI};
		formatter.readAndFormat(args, false);
	}

	/**
	 * Create an empty TraceFile object to which TraceBuffers and a TraceFileHeader can 
	 * be added.
	 */
	public TraceFile(){
		/* allow instantiation for clients that will add their own buffers */
	}

	/**
	 * Add a byte representation of a binary trace buffer to the current TraceFile.
	 * @param traceBuffer an array of bytes containing the traceBuffer to be added.
	 * The traceBuffer must have the precise length of the complete traceBuffer, and
	 * the traceBuffer must be exactly as generated by the trace engine of an IBM Java
	 * VM. The byte array must not contain any padding.
	 * @return true on success, false on failure to add the buffer to the current TraceFile.
	 * @throws InvalidTraceBufferException if the byte array does not contain a valid 
	 * binary trace buffer.
	 */
	public boolean addTraceBuffer(byte[] traceBuffer) throws InvalidTraceBufferException{
		System.err.println("addTraceBuffer not yet implemented.");
		return false;
	}
	
	/**
	 * Add a binary representation of a TraceFileHeader to the current TraceFile.
	 * @param traceFileHeader a byte array containing precisely a TraceFileHeader as 
	 * generated by the IBM Java VM trace engine. The byte array must not contain
	 * any padding.
	 * @return true if the header is added, false if a header has already been added.
	 * @throws InvalidTraceFileHeaderException if the byte array does not contain a valid
	 * binary trace file header.
	 */
	public boolean addTraceFileHeader(byte[] traceFileHeader) throws InvalidTraceFileHeaderException{
		System.err.println("addTraceFileHeader not yet implemented.");
		return false;
	}

	
	/**
	 * Get the object representation of the current TraceFile's TraceFileHeader
	 * @return a TraceFileHeader object representing the current TraceFile's TraceFileHeader,
	 * or null if the current TraceFile does not contain a valid TraceFileHeader.
	 */
	public TraceFileHeader getHeaderInfo(){
		return formatter.getTraceFileHeader();
	}
	
	/**
	 * Get an array representing all of the traced threads in the current TraceFile.
	 * @return an array of TraceThread objects, containing one TraceThread object per 
	 * thread found in the current TraceFile, or null if the current TraceFile does
	 * not contain any traced threads.
	 */
	public TraceThread[] getTraceThreads(){
		return formatter.getTraceThreads();
	}
	
	/**
	 * Get a TracePoint Iterator for the current TraceFile.
	 * @return an Iterator that can be used to walk every TracePoint in the current
	 * TraceFile in chronological order. The iterator can be used a single time only, 
	 * since it consumes the data as it walks it. Requesting an Iterator for a 
	 * consumed TraceFile with return an empty Iterator. An empty Iterator will also
	 * be returned in cases where the current TraceFile does not contain any TracePoints. 
	 */
	public Iterator getGlobalChronologicalTracePointIterator(){
		return new TracePointGlobalChronologicalIterator<TracePoint>(formatter);
	}
	
	/**
	 * Get a tracepoint from the trace file.
	 * @return the next trace point for this file.
	 */
	public TracePoint getNextTracePoint(){
		return formatter.getNextTracePoint();
	}
}

class InvalidTraceBufferException extends Exception{
	
}

class InvalidTraceFileHeaderException extends Exception{

}
