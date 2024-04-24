
/*******************************************************************************
 * Copyright (c) 1991, 2021 IBM Corp. and others
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

/**
 * @file
 * @ingroup GC_Structs
 */

#if !defined(CLASSLOCALINTERFACEITERATOR_HPP_)
#define CLASSLOCALINTERFACEITERATOR_HPP_

#include "j9.h"
#include "j9cfg.h"
#include "j9cp.h"
#include "modron.h"

/**
 * Iterate through references to the interfaces implemented by a class.
 * @ingroup GC_Structs
 */
class GC_ClassLocalInterfaceIterator
{
	J9ITable *_iTable;
	J9ITable *_superclassITable;
	
public:
	GC_ClassLocalInterfaceIterator(J9Class *clazz) :
		_iTable((J9ITable*)clazz->iTable)
	{
	
		J9Class *superclass = clazz->superclasses[J9CLASS_DEPTH(clazz) - 1];
		if(superclass) {
			_superclassITable = (J9ITable*)superclass->iTable;
		} else {
			_superclassITable = NULL;
		}
	};

	J9Class *nextSlot();
};

#endif /* CLASSLOCALINTERFACEITERATOR_HPP_ */

