/*******************************************************************************
 * Copyright (c) 2016, 2021 IBM Corp. and others
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
package com.ibm.dump.tests.javacore_deadlock;

import java.lang.Thread.State;
import java.util.concurrent.locks.ReentrantLock;

public class JUCDeadlock implements DeadlockCreator {

	final static int DEFAULT_THREADS = 10;
	
	/**
	 * Create a chain of deadlocked threads.
	 * @param args
	 */
	public void createDeadlockCycle(int threadCount) {
		Thread[] threads = new Thread[threadCount];

		ReentrantLock[] locks = new ReentrantLock[threadCount];
		for( int i = 0; i < threadCount; i++ ) {
			locks[i] = new ReentrantLock();
		}
		
		for( int i = 0; i < threadCount; i++ ) {
			threads[i] = new Thread(new DeadlockThread(locks[i%threadCount], locks[(i+1)%threadCount]));
			threads[i].setName("java.util.concurrent Deadlock Thread ##" + i + "##");
		}

		for( int i = 0; i < threadCount; i++ ) {
			threads[i].start();
		}

		boolean deadlocked = false;
		while(!deadlocked) {
			deadlocked = true;
			for( int i = 0; i < locks.length; i++ ) {
				deadlocked &= locks[i].isLocked();				
			}
			try {
				Thread.sleep(1000);
			} catch (InterruptedException e) {
			}
		}
		System.err.println(this.getClass().getSimpleName() + ": Should be able to take dump now, threads should be deadlocked.");
	}

	private static class LockObject {
		public volatile boolean taken = false;
	}
	
	private static class DeadlockThread implements Runnable {
		
		ReentrantLock prev;
		ReentrantLock next;
		
		public DeadlockThread(ReentrantLock lastLock, ReentrantLock nextLock ) {
			prev = lastLock;
			next = nextLock;
		}
		
		public void run() {
			prev.lock();
			try {
				System.out.println("Thread " + Thread.currentThread().getName() + " has last lock");
				while( next.isLocked() == false ) {
					try {
						Thread.sleep(100);
					} catch (InterruptedException e) {
						// TODO Auto-generated catch block
						e.printStackTrace();
					}
				}
				System.out.println("Thread " + Thread.currentThread().getName() + " attempting to get second lock");
				next.lock();
				try {
					System.out.println("Thread " + Thread.currentThread().getName() + " has both locks");
				} finally {
					next.unlock();
				}
			} finally {
				prev.unlock();
			}
		}
	}
	
}
