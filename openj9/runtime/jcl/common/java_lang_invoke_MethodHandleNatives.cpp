/*******************************************************************************
 * Copyright (c) 2021, 2021 IBM Corp. and others
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

#include "j9.h"
#include "jclprots.h"
#include "j9cp.h"
#include "j9protos.h"
#include "ut_j9jcl.h"
#include "j9port.h"
#include "jclglob.h"
#include "jcl_internal.h"
#include "util_api.h"
#include "j9vmconstantpool.h"
#include "ObjectAccessBarrierAPI.hpp"
#include "objhelp.h"

#include <string.h>
#include <assert.h>

#include "VMHelpers.hpp"

extern "C" {

/* Constants mapped from java.lang.invoke.MethodHandleNatives$Constants
 * These constants are validated by the MethodHandleNatives$Constants.verifyConstants()
 * method when Java assertions are enabled
 */
#define MN_IS_METHOD		0x00010000
#define MN_IS_CONSTRUCTOR	0x00020000
#define MN_IS_FIELD			0x00040000
#define MN_IS_TYPE			0x00080000
#define MN_CALLER_SENSITIVE	0x00100000

#define MN_REFERENCE_KIND_SHIFT	24
#define MN_REFERENCE_KIND_MASK	0xF		/* (flag >> MN_REFERENCE_KIND_SHIFT) & MN_REFERENCE_KIND_MASK */

#define MN_SEARCH_SUPERCLASSES	0x00100000
#define MN_SEARCH_INTERFACES	0x00200000

/* Private MemberName object init helper 
 *
 * Set the MemberName fields based on the refObject given:
 * For j.l.reflect.Field:
 *		find JNIFieldID for refObject, create j.l.String for name and signature and store in MN.name/type fields.
 *		set vmindex to the fieldID pointer and target to the J9ROMFieldShape struct.
 *		set MN.clazz to declaring class in the fieldID struct.
 * For j.l.reflect.Method or j.l.reflect.Constructor:
 *		find JNIMethodID, set vmindex to the methodID pointer and target to the J9Method struct.
 *		set MN.clazz to the refObject's declaring class.
 *
 * Then for both, compute the MN.flags using access flags and invocation type based on the JNI-id.
 *
 * Throw an IllegalArgumentException if the refObject is not a Field/Method/Constructor
 *
 * Note: caller must have vmaccess before invoking this helper
 */
static void
initImpl(J9VMThread *currentThread, j9object_t membernameObject, j9object_t refObject)
{
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;

	J9Class* refClass = J9OBJECT_CLAZZ(currentThread, refObject);

	jint flags = 0;
	jlong vmindex = 0;
	jlong target = 0;
	j9object_t clazzObject = NULL;
	j9object_t nameObject = NULL;
	j9object_t typeObject = NULL;

	if (refClass == J9VMJAVALANGREFLECTFIELD(vm)) {
		J9JNIFieldID *fieldID = vm->reflectFunctions.idFromFieldObject(currentThread, NULL, refObject);
		vmindex = (jlong)fieldID;
		target = (jlong)fieldID->field;

		flags = fieldID->field->modifiers & CFR_FIELD_ACCESS_MASK;
		flags |= MN_IS_FIELD;
		flags |= (J9_ARE_ANY_BITS_SET(flags, J9AccStatic) ? MH_REF_GETSTATIC : MH_REF_GETFIELD) << MN_REFERENCE_KIND_SHIFT;

		nameObject = J9VMJAVALANGREFLECTFIELD_NAME(currentThread, refObject);
		typeObject = J9VMJAVALANGREFLECTFIELD_TYPE(currentThread, refObject);

		clazzObject = J9VM_J9CLASS_TO_HEAPCLASS(fieldID->declaringClass);
	} else if (refClass == J9VMJAVALANGREFLECTMETHOD(vm)) {
		J9JNIMethodID *methodID = vm->reflectFunctions.idFromMethodObject(currentThread, refObject);
		vmindex = (jlong)methodID;
		target = (jlong)methodID->method;

		J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(methodID->method);

		flags = romMethod->modifiers & CFR_METHOD_ACCESS_MASK;
		if (J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccMethodCallerSensitive)) {
			flags |= MN_CALLER_SENSITIVE;
		}
		flags |= MN_IS_METHOD;
		if (J9_ARE_ANY_BITS_SET(methodID->vTableIndex, J9_JNI_MID_INTERFACE)) {
			flags |= MH_REF_INVOKEINTERFACE << MN_REFERENCE_KIND_SHIFT;
		} else if (J9_ARE_ANY_BITS_SET(romMethod->modifiers , J9AccStatic)) {
			flags |= MH_REF_INVOKESTATIC << MN_REFERENCE_KIND_SHIFT;
		} else if (J9_ARE_ANY_BITS_SET(romMethod->modifiers , J9AccFinal) || !J9ROMMETHOD_HAS_VTABLE(romMethod)) {
			flags |= MH_REF_INVOKESPECIAL << MN_REFERENCE_KIND_SHIFT;
		} else {
			flags |= MH_REF_INVOKEVIRTUAL << MN_REFERENCE_KIND_SHIFT;
		}

		nameObject = J9VMJAVALANGREFLECTMETHOD_NAME(currentThread, refObject);
		typeObject = J9VMJAVALANGREFLECTMETHOD_SIGNATURE(currentThread, refObject);
		clazzObject = J9VMJAVALANGREFLECTMETHOD_DECLARINGCLASS(currentThread, refObject);
	} else if (refClass == J9VMJAVALANGREFLECTCONSTRUCTOR(vm)) {
		J9JNIMethodID *methodID = vm->reflectFunctions.idFromConstructorObject(currentThread, refObject);
		vmindex = (jlong)methodID;
		target = (jlong)methodID->method;

		J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(methodID->method);

		flags = romMethod->modifiers & CFR_METHOD_ACCESS_MASK;
		if (J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccMethodCallerSensitive)) {
			flags |= MN_CALLER_SENSITIVE;
		}
		flags |= MN_IS_CONSTRUCTOR | (MH_REF_INVOKESPECIAL << MN_REFERENCE_KIND_SHIFT);

		typeObject = J9VMJAVALANGREFLECTCONSTRUCTOR_SIGNATURE(currentThread, refObject);
		clazzObject = J9VMJAVALANGREFLECTMETHOD_DECLARINGCLASS(currentThread, refObject);
	} else {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGILLEGALARGUMENTEXCEPTION, NULL);
	}

	if (!VM_VMHelpers::exceptionPending(currentThread)) {
		J9VMJAVALANGINVOKEMEMBERNAME_SET_FLAGS(currentThread, membernameObject, flags);
		J9VMJAVALANGINVOKEMEMBERNAME_SET_NAME(currentThread, membernameObject, nameObject);
		J9VMJAVALANGINVOKEMEMBERNAME_SET_TYPE(currentThread, membernameObject, typeObject);
		J9VMJAVALANGINVOKEMEMBERNAME_SET_CLAZZ(currentThread, membernameObject, clazzObject);
		J9OBJECT_U64_STORE(currentThread, membernameObject, vm->vmindexOffset, (U_64)vmindex);
		J9OBJECT_U64_STORE(currentThread, membernameObject, vm->vmtargetOffset, (U_64)target);
		Trc_JCL_java_lang_invoke_MethodHandleNatives_initImpl_setData(currentThread, flags, nameObject, typeObject, clazzObject, vmindex, target);
	}
}

static char *
sigForPrimitiveOrVoid(J9JavaVM *vm, J9Class *clazz)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	char* signature = (char*)j9mem_allocate_memory(2, OMRMEM_CATEGORY_VM);

	if (NULL != signature) {
		if (clazz == vm->booleanReflectClass) {
			signature[0] = 'Z';
		} else if (clazz == vm->byteReflectClass) {
			signature[0] = 'B';
		} else if (clazz == vm->charReflectClass) {
			signature[0] = 'C';
		} else if (clazz == vm->shortReflectClass) {
			signature[0] = 'S';
		} else if (clazz == vm->intReflectClass) {
			signature[0] = 'I';
		} else if (clazz == vm->longReflectClass) {
			signature[0] = 'J';
		} else if (clazz == vm->floatReflectClass) {
			signature[0] = 'F';
		} else if (clazz == vm->doubleReflectClass) {
			signature[0] = 'D';
		} else if (clazz == vm->voidReflectClass) {
			signature[0] = 'V';
		}

		signature[1] = '\0';
	}

	return signature;
}

static char *
getClassSignature(J9VMThread *currentThread, J9Class * clazz)
{
	J9JavaVM *vm = currentThread->javaVM;
	PORT_ACCESS_FROM_JAVAVM(vm);
	char *sig = NULL;

	if (J9ROMCLASS_IS_PRIMITIVE_TYPE(clazz->romClass)) {
		sig = sigForPrimitiveOrVoid(vm, clazz);
	} else {
		j9object_t sigString = J9VMJAVALANGCLASS_CLASSNAMESTRING(currentThread, J9VM_J9CLASS_TO_HEAPCLASS(clazz));
		if (NULL != sigString) {
			/* +3 so that we can fit 'L' and ';' around the class name with the null-terminator */
			UDATA utfLength = vm->internalVMFunctions->getStringUTF8Length(currentThread, sigString) + 3;
			sig = (char *)j9mem_allocate_memory(utfLength, OMRMEM_CATEGORY_VM);
			if (NULL != sig) {
				if (J9ROMCLASS_IS_ARRAY(clazz->romClass)) {
					vm->internalVMFunctions->copyStringToUTF8Helper(currentThread, sigString, J9_STR_NULL_TERMINATE_RESULT | J9_STR_XLAT, 0, J9VMJAVALANGSTRING_LENGTH(currentThread, sigString), (U_8*)sig, utfLength);
				} else {
					sig[0] = 'L';
					vm->internalVMFunctions->copyStringToUTF8Helper(currentThread, sigString, J9_STR_XLAT, 0, J9VMJAVALANGSTRING_LENGTH(currentThread, sigString), (U_8*)(sig + 1), utfLength - 1);
					sig[utfLength - 2] = ';';
					sig[utfLength - 1] = '\0';
				}
			}
		} else {
			U_32 numDims = 0;

			J9Class *myClass = clazz;
			while (J9ROMCLASS_IS_ARRAY(myClass->romClass)) {
				J9Class * componentClass = (J9Class *)(((J9ArrayClass*)myClass)->componentType);
				if (J9ROMCLASS_IS_PRIMITIVE_TYPE(componentClass->romClass)) {
					break;
				}
				numDims += 1;
				myClass = componentClass;
			}

			J9UTF8 *romName = J9ROMCLASS_CLASSNAME(myClass->romClass);
			U_32 nameLength = J9UTF8_LENGTH(romName);
			char * name = (char *)J9UTF8_DATA(romName);
			U_32 sigLength = nameLength + numDims;
			if (* name != '[') {
				sigLength += 2;
			}

			sigLength++; /* for null-termination */
			sig = (char *)j9mem_allocate_memory(sigLength, OMRMEM_CATEGORY_VM);
			if (NULL != sig) {
				U_32 i = 0;
				for (i = 0; i < numDims; i++) {
					sig[i] = '[';
				}

				if (*name != '[') {
					sig[i++] = 'L';
				}

				memcpy(sig+i, name, nameLength);
				i += nameLength;

				if (*name != '[') {
					sig[i++] = ';';
				}
				sig[sigLength-1] = '\0';
			}
		}
	}

	return sig;
}

static char *
getSignatureFromMethodType(J9VMThread *currentThread, j9object_t typeObject, UDATA *signatureLength)
{
	J9JavaVM *vm = currentThread->javaVM;
	j9object_t ptypes = J9VMJAVALANGINVOKEMETHODTYPE_PTYPES(currentThread, typeObject);
	U_32 numArgs = J9INDEXABLEOBJECT_SIZE(currentThread, ptypes);

	char* methodDescriptor = NULL;
	char* cursor = NULL;
	char* rSignature = NULL;

	PORT_ACCESS_FROM_JAVAVM(vm);

	char** signatures = (char**)j9mem_allocate_memory((numArgs) * sizeof(char*), OMRMEM_CATEGORY_VM);
	if (NULL == signatures) {
		goto done;
	}

	memset(signatures, 0, (numArgs) * sizeof(char*));

	*signatureLength = 2; /* space for '(', ')' */
	for (U_32 i = 0; i < numArgs; i++) {
		j9object_t pObject = J9JAVAARRAYOFOBJECT_LOAD(currentThread, ptypes, i);
		J9Class *pclass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, pObject);
		signatures[i] = getClassSignature(currentThread, pclass);
		if (NULL == signatures[i]) {
			goto done;
		}

		*signatureLength += strlen(signatures[i]);
	}

	{
		/* Return type */
		j9object_t rtype = J9VMJAVALANGINVOKEMETHODTYPE_RTYPE(currentThread, typeObject);
		J9Class *rclass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, rtype);
		rSignature = getClassSignature(currentThread, rclass);
		if (NULL == rSignature) {
			goto done;
		}
	}

	*signatureLength += strlen(rSignature);

	methodDescriptor = (char*)j9mem_allocate_memory(*signatureLength+1, OMRMEM_CATEGORY_VM);
	if (NULL == methodDescriptor) {
		goto done;
	}
	cursor = methodDescriptor;
	*cursor++ = '(';

	/* Copy class signatures to descriptor string */
	for (U_32 i = 0; i < numArgs; i++) {
		UDATA len = strlen(signatures[i]);
		strncpy(cursor, signatures[i], len);
		cursor += len;
	}

	*cursor++ = ')';
	/* Copy return type signature to descriptor string */
	strcpy(cursor, rSignature);

done:
	j9mem_free_memory(rSignature);

	if (NULL != signatures) {
		for (U_32 i = 0; i < numArgs; i++) {
			j9mem_free_memory(signatures[i]);
		}
		j9mem_free_memory(signatures);
	}
	return methodDescriptor;
}

j9object_t
resolveRefToObject(J9VMThread *currentThread, J9ConstantPool *ramConstantPool, U_16 cpIndex, bool resolve)
{
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;

	j9object_t result = NULL;

	J9RAMSingleSlotConstantRef *ramCP = (J9RAMSingleSlotConstantRef*)ramConstantPool + cpIndex;
	U_32 *cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(J9_CLASS_FROM_CP(ramConstantPool)->romClass);

	switch (J9_CP_TYPE(cpShapeDescription, cpIndex)) {
	case J9CPTYPE_CLASS: {
		J9Class *clazz = (J9Class*)ramCP->value;
		if ((NULL == clazz) && resolve) {
			clazz = vmFuncs->resolveClassRef(currentThread, ramConstantPool, cpIndex, J9_RESOLVE_FLAG_RUNTIME_RESOLVE);
		}
		if (NULL != clazz) {
			result = J9VM_J9CLASS_TO_HEAPCLASS(clazz);
		}
		break;
	}
	case J9CPTYPE_STRING: {
		result = (j9object_t)ramCP->value;
		if ((NULL == result) && resolve) {
			result = vmFuncs->resolveStringRef(currentThread, ramConstantPool, cpIndex, J9_RESOLVE_FLAG_RUNTIME_RESOLVE);
		}
		break;
	}
	case J9CPTYPE_INT: {
		J9ROMSingleSlotConstantRef *romCP = (J9ROMSingleSlotConstantRef*)J9_ROM_CP_FROM_CP(ramConstantPool) + cpIndex;
		result = vm->memoryManagerFunctions->J9AllocateObject(currentThread, J9VMJAVALANGINTEGER_OR_NULL(vm), J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (NULL == result) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
			goto done;
		}
		J9VMJAVALANGINTEGER_SET_VALUE(currentThread, result, romCP->data);
		break;
	}
	case J9CPTYPE_FLOAT: {
		J9ROMSingleSlotConstantRef *romCP = (J9ROMSingleSlotConstantRef*)J9_ROM_CP_FROM_CP(ramConstantPool) + cpIndex;
		result = vm->memoryManagerFunctions->J9AllocateObject(currentThread, J9VMJAVALANGFLOAT_OR_NULL(vm), J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (NULL == result) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
			goto done;
		}
		J9VMJAVALANGFLOAT_SET_VALUE(currentThread, result, romCP->data);
		break;
	}
	case J9CPTYPE_LONG: {
		J9ROMConstantRef *romCP = (J9ROMConstantRef*)J9_ROM_CP_FROM_CP(ramConstantPool) + cpIndex;
#ifdef J9VM_ENV_LITTLE_ENDIAN
		U_64 value = (((U_64)(romCP->slot2)) << 32) | ((U_64)(romCP->slot1));
#else
		U_64 value = (((U_64)(romCP->slot1)) << 32) | ((U_64)(romCP->slot2));
#endif
		result = vm->memoryManagerFunctions->J9AllocateObject(currentThread, J9VMJAVALANGLONG_OR_NULL(vm), J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (NULL == result) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
			goto done;
		}
		J9VMJAVALANGLONG_SET_VALUE(currentThread, result, value);
		break;
	}
	case J9CPTYPE_DOUBLE: {
		J9ROMConstantRef *romCP = (J9ROMConstantRef*)J9_ROM_CP_FROM_CP(ramConstantPool) + cpIndex;
#ifdef J9VM_ENV_LITTLE_ENDIAN
		U_64 value = (((U_64)(romCP->slot2)) << 32) | ((U_64)(romCP->slot1));
#else
		U_64 value = (((U_64)(romCP->slot1)) << 32) | ((U_64)(romCP->slot2));
#endif
		result = vm->memoryManagerFunctions->J9AllocateObject(currentThread, J9VMJAVALANGDOUBLE_OR_NULL(vm), J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		if (NULL == result) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
			goto done;
		}
		J9VMJAVALANGDOUBLE_SET_VALUE(currentThread, result, value);
		break;
	}
	case J9CPTYPE_METHOD_TYPE: {
		result = (j9object_t)ramCP->value;
		if ((NULL == result) && resolve) {
			result = vmFuncs->resolveMethodTypeRef(currentThread, ramConstantPool, cpIndex, J9_RESOLVE_FLAG_RUNTIME_RESOLVE);
		}
		break;
	}
	case J9CPTYPE_METHODHANDLE: {
		result = (j9object_t)ramCP->value;
		if ((NULL == result) && resolve) {
			result = vmFuncs->resolveMethodHandleRef(currentThread, ramConstantPool, cpIndex, J9_RESOLVE_FLAG_RUNTIME_RESOLVE | J9_RESOLVE_FLAG_NO_CLASS_INIT);
		}
		break;
	}
	case J9CPTYPE_CONSTANT_DYNAMIC: {
		result = (j9object_t)ramCP->value;
		if ((NULL == result) && resolve) {
			result = vmFuncs->resolveConstantDynamic(currentThread, ramConstantPool, cpIndex, J9_RESOLVE_FLAG_RUNTIME_RESOLVE);
		}
		break;
	}
	} /* switch */
done:
	return result;
}

J9Method *
lookupMethod(J9VMThread *currentThread, J9Class *resolvedClass, J9JNINameAndSignature *nas, J9Class *callerClass, UDATA lookupOptions)
{
	J9Method *result = NULL;

	/* If looking for a MethodHandle polymorphic INL method, allow any caller signature. */
	if (resolvedClass == J9VMJAVALANGINVOKEMETHODHANDLE(currentThread->javaVM)) {
		if ((0 == strcmp(nas->name, "linkToVirtual"))
		|| (0 == strcmp(nas->name, "linkToStatic"))
		|| (0 == strcmp(nas->name, "linkToSpecial"))
		|| (0 == strcmp(nas->name, "linkToInterface"))
		|| (0 == strcmp(nas->name, "invokeBasic"))
		) {
			nas->signature = NULL;
			nas->signatureLength = 0;

			/* Set flag for partial signature lookup. Signature length is already initialized to 0. */
			lookupOptions |= J9_LOOK_PARTIAL_SIGNATURE;
		}
	}

	result = (J9Method*)currentThread->javaVM->internalVMFunctions->javaLookupMethod(currentThread, resolvedClass, (J9ROMNameAndSignature*)nas, callerClass, lookupOptions);

	return result;
}

static void
setCallSiteTargetImpl(J9VMThread *currentThread, jobject callsite, jobject target, bool isVolatile)
{
	const J9InternalVMFunctions *vmFuncs = currentThread->javaVM->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	if ((NULL == callsite) || (NULL == target)) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		j9object_t callsiteObject = J9_JNI_UNWRAP_REFERENCE(callsite);
		j9object_t targetObject = J9_JNI_UNWRAP_REFERENCE(target);

		UDATA offset = (UDATA)vmFuncs->instanceFieldOffset(currentThread, J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, callsiteObject), (U_8*)"target", strlen("target"), (U_8*)"Ljava/lang/invoke/MethodHandle;", strlen("Ljava/lang/invoke/MethodHandle;"), NULL, NULL, 0);
		MM_ObjectAccessBarrierAPI objectAccessBarrier = MM_ObjectAccessBarrierAPI(currentThread);
		objectAccessBarrier.inlineMixedObjectStoreObject(currentThread, callsiteObject, offset, targetObject, isVolatile);
	}
	vmFuncs->internalExitVMToJNI(currentThread);
}


/**
 * static native void init(MemberName self, Object ref);
 *
 * Initializes a MemberName object using the given ref object.
 *		see initImpl for detail
 * Throw NPE if self or ref is null
 * Throw IllegalArgumentException if ref is not a field/method/constructor
 */
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_init(JNIEnv *env, jclass clazz, jobject self, jobject ref)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	Trc_JCL_java_lang_invoke_MethodHandleNatives_init_Entry(env, self, ref);
	if ((NULL == self) || (NULL == ref)) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		j9object_t membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
		j9object_t refObject = J9_JNI_UNWRAP_REFERENCE(ref);

		initImpl(currentThread, membernameObject, refObject);
	}
	Trc_JCL_java_lang_invoke_MethodHandleNatives_init_Exit(env);
	vmFuncs->internalExitVMToJNI(currentThread);
}

/**
 * static native void expand(MemberName self);
 *
 * Given a MemberName object, try to set the uninitialized fields from existing VM metadata.
 * Uses VM metadata (vmindex & vmtarget) to set symblic data fields (name & type & defc)
 *
 * Throws NullPointerException if MemberName object is null.
 * Throws IllegalArgumentException if MemberName doesn't contain required data to expand.
 * Throws InternalError if the MemberName object contains invalid data or completely uninitialized.
 */
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_expand(JNIEnv *env, jclass clazz, jobject self)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	Trc_JCL_java_lang_invoke_MethodHandleNatives_expand_Entry(env, self);
	if (NULL == self) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		j9object_t membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
		jint flags = J9VMJAVALANGINVOKEMEMBERNAME_FLAGS(currentThread, membernameObject);
		jlong vmindex = (jlong)J9OBJECT_ADDRESS_LOAD(currentThread, membernameObject, vm->vmindexOffset);

		Trc_JCL_java_lang_invoke_MethodHandleNatives_expand_Data(env, membernameObject, flags, vmindex);
		if (J9_ARE_ANY_BITS_SET(flags, MN_IS_FIELD)) {
			/* For Field MemberName, the clazz and vmindex fields must be set. */
			if ((NULL != J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, membernameObject)) && (NULL != (void*)vmindex)) {
				J9JNIFieldID *field = (J9JNIFieldID*)vmindex;

				/* if name/type field is uninitialized, create j.l.String from ROM field name/sig and store in MN fields. */
				if (NULL == J9VMJAVALANGINVOKEMEMBERNAME_NAME(currentThread, membernameObject)) {
					J9UTF8 *name = J9ROMFIELDSHAPE_NAME(field->field);
					j9object_t nameString = vm->memoryManagerFunctions->j9gc_createJavaLangString(currentThread, J9UTF8_DATA(name), (U_32)J9UTF8_LENGTH(name), J9_STR_INTERN);
					if (NULL != nameString) {
						/* Refetch reference after GC point */
						membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
						J9VMJAVALANGINVOKEMEMBERNAME_SET_NAME(currentThread, membernameObject, nameString);
					}
				}
				if (NULL == J9VMJAVALANGINVOKEMEMBERNAME_TYPE(currentThread, membernameObject)) {
					J9UTF8 *signature = J9ROMFIELDSHAPE_SIGNATURE(field->field);
					j9object_t signatureString = vm->memoryManagerFunctions->j9gc_createJavaLangString(currentThread, J9UTF8_DATA(signature), (U_32)J9UTF8_LENGTH(signature), J9_STR_INTERN);
					if (NULL != signatureString) {
						/* Refetch reference after GC point */
						membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
						J9VMJAVALANGINVOKEMEMBERNAME_SET_TYPE(currentThread, membernameObject, signatureString);
					}
				}
			} else {
				vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGILLEGALARGUMENTEXCEPTION, NULL);
			}
		} else if (J9_ARE_ANY_BITS_SET(flags, MN_IS_METHOD | MN_IS_CONSTRUCTOR)) {
			if (NULL != (void*)vmindex) {
				/* For method/constructor MemberName, the vmindex field is required for expand.*/
				J9JNIMethodID *methodID = (J9JNIMethodID*)vmindex;
				J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(methodID->method);

				/* Retrieve method info using JNIMethodID, store to MN fields. */
				if (NULL == J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, membernameObject)) {
					j9object_t newClassObject = J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_METHOD(methodID->method));
					J9VMJAVALANGINVOKEMEMBERNAME_SET_CLAZZ(currentThread, membernameObject, newClassObject);
				}
				if (NULL == J9VMJAVALANGINVOKEMEMBERNAME_NAME(currentThread, membernameObject)) {
					J9UTF8 *name = J9ROMMETHOD_NAME(romMethod);
					j9object_t nameString = vm->memoryManagerFunctions->j9gc_createJavaLangString(currentThread, J9UTF8_DATA(name), (U_32)J9UTF8_LENGTH(name), J9_STR_INTERN);
					if (NULL != nameString) {
						/* Refetch reference after GC point */
						membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
						J9VMJAVALANGINVOKEMEMBERNAME_SET_NAME(currentThread, membernameObject, nameString);
					}
				}
				if (NULL == J9VMJAVALANGINVOKEMEMBERNAME_TYPE(currentThread, membernameObject)) {
					J9UTF8 *signature = J9ROMMETHOD_SIGNATURE(romMethod);
					j9object_t signatureString = vm->memoryManagerFunctions->j9gc_createJavaLangString(currentThread, J9UTF8_DATA(signature), (U_32)J9UTF8_LENGTH(signature), J9_STR_INTERN);
					if (NULL != signatureString) {
						/* Refetch reference after GC point */
						membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
						J9VMJAVALANGINVOKEMEMBERNAME_SET_TYPE(currentThread, membernameObject, signatureString);
					}
				}
			} else {
				vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGILLEGALARGUMENTEXCEPTION, NULL);
			}
		} else {
			vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
		}
	}
	Trc_JCL_java_lang_invoke_MethodHandleNatives_expand_Exit(env);
	vmFuncs->internalExitVMToJNI(currentThread);
}

/**
 * static native MemberName resolve(MemberName self, Class<?> caller,
 *      boolean speculativeResolve) throws LinkageError, ClassNotFoundException;
 *
 * Resolve the method/field represented by the MemberName's symbolic data (name & type & defc) with the supplied caller
 * Store the resolved Method/Field's JNI-id in vmindex, field offset / method pointer in vmtarget
 *
 * If the speculativeResolve flag is not set, failed resolution will throw the corresponding exception.
 * If the resolution failed with no exception:
 * Throw NoSuchFieldError for field MemberName
 * Throw NoSuchMethodError for method/constructor MemberName
 * Throw LinkageError for other
 */
jobject JNICALL
Java_java_lang_invoke_MethodHandleNatives_resolve(JNIEnv *env, jclass clazz, jobject self, jclass caller, jboolean speculativeResolve)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jobject result = NULL;
	char *name = NULL;
	char *signature = NULL;
	PORT_ACCESS_FROM_JAVAVM(vm);
	vmFuncs->internalEnterVMFromJNI(currentThread);

	Trc_JCL_java_lang_invoke_MethodHandleNatives_resolve_Entry(env, self, caller, (speculativeResolve ? "true" : "false"));
	if (NULL == self) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
	} else {
		j9object_t membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
		jlong vmindex = (jlong)(UDATA)J9OBJECT_U64_LOAD(currentThread, membernameObject, vm->vmindexOffset);
		jlong target = (jlong)(UDATA)J9OBJECT_U64_LOAD(currentThread, membernameObject, vm->vmtargetOffset);

		jint flags = J9VMJAVALANGINVOKEMEMBERNAME_FLAGS(currentThread, membernameObject);
		jint new_flags = 0;
		j9object_t new_clazz = NULL;

		Trc_JCL_java_lang_invoke_MethodHandleNatives_resolve_Data(env, membernameObject, caller, flags, vmindex, target);
		/* Check if MemberName is already resolved */
		if (0 != target) {
			result = self;
		} else {
			/* Initialize nameObject after creating typeString which could trigger GC */
			j9object_t nameObject = NULL;
			j9object_t typeObject = J9VMJAVALANGINVOKEMEMBERNAME_TYPE(currentThread, membernameObject);
			j9object_t clazzObject = J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, membernameObject);
			J9Class *resolvedClass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, clazzObject);

			jint ref_kind = (flags >> MN_REFERENCE_KIND_SHIFT) & MN_REFERENCE_KIND_MASK;

			UDATA nameLength = 0;
			UDATA signatureLength = 0;
			J9Class *typeClass = J9OBJECT_CLAZZ(currentThread, typeObject);

			/* The type field of a MemberName could be in:
			 *     MethodType:	MethodType representing method/constructor MemberName's method signature
			 *     String:		String representing MemberName's signature (field or method)
			 *     Class:		Class representing field MemberName's field type
			 */
			if (J9VMJAVALANGINVOKEMETHODTYPE(vm) == typeClass) {
				j9object_t sigString = J9VMJAVALANGINVOKEMETHODTYPE_METHODDESCRIPTOR(currentThread, typeObject);
				if (NULL != sigString) {
					signature = vmFuncs->copyStringToUTF8WithMemAlloc(currentThread, sigString, J9_STR_NULL_TERMINATE_RESULT | J9_STR_XLAT, "", 0, NULL, 0, &signatureLength);
				} else {
					signature = getSignatureFromMethodType(currentThread, typeObject, &signatureLength);
					// TODO store this signature as j.l.String in MT
				}
			} else if (J9VMJAVALANGSTRING_OR_NULL(vm) == typeClass) {
				signature = vmFuncs->copyStringToUTF8WithMemAlloc(currentThread, typeObject, J9_STR_NULL_TERMINATE_RESULT | J9_STR_XLAT, "", 0, NULL, 0, &signatureLength);
			} else if (J9VMJAVALANGCLASS(vm) == typeClass) {
				J9Class *rclass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, typeObject);
				signature = getClassSignature(currentThread, rclass);
				if (NULL != signature) {
					signatureLength = strlen(signature);
				}
			} else {
				vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
				goto done;
			}

			/* Check if signature string is correctly generated */
			if (NULL == signature) {
				if (!VM_VMHelpers::exceptionPending(currentThread)) {
					vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
				}
				goto done;
			}

			/* Refetch reference after GC point */
			membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
			nameObject = J9VMJAVALANGINVOKEMEMBERNAME_NAME(currentThread, membernameObject);
			name = vmFuncs->copyStringToUTF8WithMemAlloc(currentThread, nameObject, J9_STR_NULL_TERMINATE_RESULT, "", 0, NULL, 0, &nameLength);
			if (NULL == name) {
				vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
				goto done;
			}

			Trc_JCL_java_lang_invoke_MethodHandleNatives_resolve_NAS(env, name, signature);

			if (J9_ARE_ANY_BITS_SET(flags, MN_IS_METHOD | MN_IS_CONSTRUCTOR)) {
				j9object_t callerObject = (NULL == caller) ? NULL : J9_JNI_UNWRAP_REFERENCE(caller);
				J9Class *callerClass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, callerObject);

				J9JNINameAndSignature nas;
				UDATA lookupOptions = J9_LOOK_JNI;

				if (JNI_TRUE == speculativeResolve) {
					lookupOptions |= J9_LOOK_NO_THROW;
				}

				/* Determine the lookup type based on reference kind and resolved class flags */
				switch (ref_kind)
				{
					case MH_REF_INVOKEINTERFACE:
						lookupOptions |= J9_LOOK_INTERFACE;
						break;
					case MH_REF_INVOKESPECIAL:
						lookupOptions |= (J9_LOOK_VIRTUAL | J9_LOOK_ALLOW_FWD | J9_LOOK_HANDLE_DEFAULT_METHOD_CONFLICTS);
						break;
					case MH_REF_INVOKESTATIC:
						lookupOptions |= J9_LOOK_STATIC;
						if (J9_ARE_ANY_BITS_SET(resolvedClass->romClass->modifiers, J9AccInterface)) {
							lookupOptions |= J9_LOOK_INTERFACE;
						}
						break;
					default:
						lookupOptions |= J9_LOOK_VIRTUAL;
						break;
				}

				nas.name = name;
				nas.signature = signature;
				nas.nameLength = (U_32)nameLength;
				nas.signatureLength = (U_32)signatureLength;

				/* Check if signature polymorphic native calls */
				J9Method *method = lookupMethod(currentThread, resolvedClass, &nas, callerClass, lookupOptions);

				/* Check for resolution exception */
				if (VM_VMHelpers::exceptionPending(currentThread)) {
					goto done;
				}

				if (NULL != method) {
					J9JNIMethodID *methodID = vmFuncs->getJNIMethodID(currentThread, method);
					vmindex = (jlong)(UDATA)methodID;
					target = (jlong)(UDATA)method;

					new_clazz = J9VM_J9CLASS_TO_HEAPCLASS(J9_CLASS_FROM_METHOD(method));

					J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(methodID->method);
					new_flags = flags | (romMethod->modifiers & CFR_METHOD_ACCESS_MASK);
					if (J9_ARE_ANY_BITS_SET(romMethod->modifiers, J9AccMethodCallerSensitive)) {
						new_flags |= MN_CALLER_SENSITIVE;
					}
				}
			} if (J9_ARE_ANY_BITS_SET(flags, MN_IS_FIELD)) {
				J9Class *declaringClass;
				J9ROMFieldShape *romField;
				UDATA lookupOptions = 0;
				UDATA offset = 0;
				if (JNI_TRUE == speculativeResolve) {
					lookupOptions |= J9_RESOLVE_FLAG_NO_THROW_ON_FAIL;
				}

				/* MemberName doesn't differentiate if a field is static or not,
				 * the resolve code have to attempt to resolve as instance field first,
				 * then as static field if the first attempt failed.
				 */
				offset = vmFuncs->instanceFieldOffset(currentThread,
					resolvedClass,
					(U_8*)name, strlen(name),
					(U_8*)signature, strlen(signature),
					&declaringClass, (UDATA*)&romField,
					lookupOptions);

				if (offset == (UDATA)-1) {
					declaringClass = NULL;

					if (VM_VMHelpers::exceptionPending(currentThread)) {
						VM_VMHelpers::clearException(currentThread);
					}

					void* fieldAddress = vmFuncs->staticFieldAddress(currentThread,
						resolvedClass,
						(U_8*)name, strlen(name),
						(U_8*)signature, strlen(signature),
						&declaringClass, (UDATA*)&romField,
						lookupOptions,
						NULL);

					if (fieldAddress == NULL) {
						declaringClass = NULL;
					} else {
						offset = (UDATA)fieldAddress - (UDATA)declaringClass->ramStatics;
					}
				}

				if (NULL != declaringClass) {
					UDATA inconsistentData = 0;
					J9JNIFieldID *fieldID = vmFuncs->getJNIFieldID(currentThread, declaringClass, romField, offset, &inconsistentData);
					vmindex = (jlong)(UDATA)fieldID;

					new_clazz = J9VM_J9CLASS_TO_HEAPCLASS(declaringClass);
					new_flags = MN_IS_FIELD | (fieldID->field->modifiers & CFR_FIELD_ACCESS_MASK);
					romField = fieldID->field;

					if (J9_ARE_ANY_BITS_SET(romField->modifiers, J9AccStatic)) {
						offset = fieldID->offset | J9_SUN_STATIC_FIELD_OFFSET_TAG;
						if (J9_ARE_ANY_BITS_SET(romField->modifiers, J9AccFinal)) {
							offset |= J9_SUN_FINAL_FIELD_OFFSET_TAG;
						}

						if ((MH_REF_PUTFIELD == ref_kind) || (MH_REF_PUTSTATIC == ref_kind)) {
							new_flags |= (MH_REF_PUTSTATIC << MN_REFERENCE_KIND_SHIFT);
						} else {
							new_flags |= (MH_REF_GETSTATIC << MN_REFERENCE_KIND_SHIFT);
						}
					} else {
						if ((MH_REF_PUTFIELD == ref_kind) || (MH_REF_PUTSTATIC == ref_kind)) {
							new_flags |= (MH_REF_PUTFIELD << MN_REFERENCE_KIND_SHIFT);
						} else {
							new_flags |= (MH_REF_GETFIELD << MN_REFERENCE_KIND_SHIFT);
						}
					}

					target = (jlong)offset;
				}
			}

			if ((0 != vmindex) && (0 != target)) {
				/* Refetch reference after GC point */
				membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
				J9VMJAVALANGINVOKEMEMBERNAME_SET_FLAGS(currentThread, membernameObject, new_flags);
				J9VMJAVALANGINVOKEMEMBERNAME_SET_CLAZZ(currentThread, membernameObject, new_clazz);
				J9OBJECT_U64_STORE(currentThread, membernameObject, vm->vmindexOffset, (U_64)vmindex);
				J9OBJECT_U64_STORE(currentThread, membernameObject, vm->vmtargetOffset, (U_64)target);

				Trc_JCL_java_lang_invoke_MethodHandleNatives_resolve_resolved(env, vmindex, target, new_clazz, flags);

				result = vmFuncs->j9jni_createLocalRef(env, membernameObject);
			}

			if ((NULL == result) && (JNI_TRUE != speculativeResolve) && !VM_VMHelpers::exceptionPending(currentThread)) {
				if (J9_ARE_ANY_BITS_SET(flags, MN_IS_FIELD)) {
					vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNOSUCHFIELDERROR, NULL);
				} else if (J9_ARE_ANY_BITS_SET(flags, MN_IS_CONSTRUCTOR | MN_IS_METHOD)) {
					vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNOSUCHMETHODERROR, NULL);
				} else {
					vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGLINKAGEERROR, NULL);
				}
			}
		}
	}

done:
	j9mem_free_memory(name);
	j9mem_free_memory(signature);

	if ((JNI_TRUE == speculativeResolve) && VM_VMHelpers::exceptionPending(currentThread)) {
		VM_VMHelpers::clearException(currentThread);
	}
	Trc_JCL_java_lang_invoke_MethodHandleNatives_resolve_Exit(env);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

/**
 * static native int getMembers(Class<?> defc, String matchName, String matchSig,
 *      int matchFlags, Class<?> caller, int skip, MemberName[] results);
 *
 * Search the defc (defining class) chain for field/method that matches the given search parameters,
 * for each matching result found, initialize the next MemberName object in results array to reference the found field/method
 *
 *  - defc: the class to start the search
 *  - matchName: name to match, NULL to match any field/method name
 *  - matchSig: signature to match, NULL to match any field/method type
 *  - matchFlags: flags defining search options:
 *  		MN_IS_FIELD - search for fields
 *  		MN_IS_CONSTRUCTOR | MN_IS_METHOD - search for method/constructor
 *  		MN_SEARCH_SUPERCLASSES - search the superclasses of the defining class
 *  		MN_SEARCH_INTERFACES - search the interfaces implemented by the defining class
 *  - caller: the caller class performing the lookup
 *  - skip: number of matching results to skip before storing
 *  - results: an array of MemberName objects to hold the matched field/method
 */
jint JNICALL
Java_java_lang_invoke_MethodHandleNatives_getMembers(JNIEnv *env, jclass clazz, jclass defc, jstring matchName, jstring matchSig, jint matchFlags, jclass caller, jint skip, jobjectArray results)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	jint result = 0;
	J9UTF8 *name = NULL;
	J9UTF8 *sig = NULL;
	j9object_t callerObject = ((NULL == caller) ? NULL : J9_JNI_UNWRAP_REFERENCE(caller));

	PORT_ACCESS_FROM_JAVAVM(vm);

	Trc_JCL_java_lang_invoke_MethodHandleNatives_getMembers_Entry(env, defc, matchName, matchSig, matchFlags, caller, skip, results);

	if ((NULL == defc) || (NULL == results) || ((NULL != callerObject) && (J9VMJAVALANGCLASS(vm) != J9OBJECT_CLAZZ(currentThread, callerObject)))) {
		result = -1;
	} else {
		j9object_t defcObject = J9_JNI_UNWRAP_REFERENCE(defc);
		J9Class *defClass = J9VM_J9CLASS_FROM_HEAPCLASS(currentThread, defcObject);

		if (NULL != matchName) {
			name = vmFuncs->copyStringToJ9UTF8WithMemAlloc(currentThread, J9_JNI_UNWRAP_REFERENCE(matchName), J9_STR_NONE, "", 0, NULL, 0);
			if (NULL == name) {
				vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
				goto done;
			}
		}
		if (NULL != matchSig) {
			sig = vmFuncs->copyStringToJ9UTF8WithMemAlloc(currentThread, J9_JNI_UNWRAP_REFERENCE(matchSig), J9_STR_NONE, "", 0, NULL, 0);
			if (NULL == sig) {
				vmFuncs->setNativeOutOfMemoryError(currentThread, 0, 0);
				goto done;
			}
		}

		if (!(((NULL != matchName) && (0 == J9UTF8_LENGTH(name))) || ((NULL != matchSig) && (0 == J9UTF8_LENGTH(sig))))) {
			j9array_t resultsArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(results);
			UDATA length = J9INDEXABLEOBJECT_SIZE(currentThread, resultsArray);
			UDATA index = 0;

			if (J9ROMCLASS_IS_INTERFACE(defClass->romClass)) {
				result = -1;
			} else {
				if (J9_ARE_ANY_BITS_SET(matchFlags, MN_IS_FIELD)) {
					J9ROMFieldShape *romField = NULL;
					J9ROMFieldWalkState walkState;

					UDATA classDepth = 0;
					if (J9_ARE_ANY_BITS_SET(matchFlags, MN_SEARCH_SUPERCLASSES)) {
						/* walk superclasses */
						J9CLASS_DEPTH(defClass);
					}
					J9Class *currentClass = defClass;

					while (NULL != currentClass) {
						/* walk currentClass */
						memset(&walkState, 0, sizeof(walkState));
						romField = romFieldsStartDo(currentClass->romClass, &walkState);

						while (NULL != romField) {
							J9UTF8 *nameUTF = J9ROMFIELDSHAPE_NAME(romField);
							J9UTF8 *signatureUTF = J9ROMFIELDSHAPE_SIGNATURE(romField);

							if (((NULL == matchName) || J9UTF8_EQUALS(name, nameUTF))
							&& ((NULL == matchSig) || J9UTF8_EQUALS(sig, signatureUTF))
							) {
								if (skip > 0) {
									skip -=1;
								} else {
									if (index < length) {
										/* Refetch reference after GC point */
										resultsArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(results);
										j9object_t memberName = J9JAVAARRAYOFOBJECT_LOAD(currentThread, resultsArray, index);
										if (NULL == memberName) {
											result = -99;
											goto done;
										}
										PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, memberName);

										/* create static field object */
										j9object_t fieldObj = vm->reflectFunctions.createFieldObject(currentThread, romField, defClass, (romField->modifiers & J9AccStatic) == J9AccStatic);
										memberName = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);

										if (NULL != fieldObj) {
											initImpl(currentThread, memberName, fieldObj);
										}
										if (VM_VMHelpers::exceptionPending(currentThread)) {
											goto done;
										}
									}
								}
								result += 1;
							}
							romField = romFieldsNextDo(&walkState);
						}

						/* get the superclass */
						if (classDepth >= 1) {
							classDepth -= 1;
							currentClass = defClass->superclasses[classDepth];
						} else {
							currentClass = NULL;
						}
					}

					/* walk interfaces */
					if (J9_ARE_ANY_BITS_SET(matchFlags, MN_SEARCH_INTERFACES)) {
						J9ITable *currentITable = (J9ITable *)defClass->iTable;

						while (NULL != currentITable) {
							memset(&walkState, 0, sizeof(walkState));
							romField = romFieldsStartDo(currentITable->interfaceClass->romClass, &walkState);
							while (NULL != romField) {
								J9UTF8 *nameUTF = J9ROMFIELDSHAPE_NAME(romField);
								J9UTF8 *signatureUTF = J9ROMFIELDSHAPE_SIGNATURE(romField);

								if (((NULL == matchName) || J9UTF8_EQUALS(name, nameUTF))
								&& ((NULL == matchSig) || J9UTF8_EQUALS(sig, signatureUTF))
								) {
									if (skip > 0) {
										skip -=1;
									} else {
										if (index < length) {
											/* Refetch reference after GC point */
											resultsArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(results);
											j9object_t memberName = J9JAVAARRAYOFOBJECT_LOAD(currentThread, resultsArray, index);
											if (NULL == memberName) {
												result = -99;
												goto done;
											}
											PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, memberName);

											/* create field object */
											j9object_t fieldObj = vm->reflectFunctions.createFieldObject(currentThread, romField, defClass, (romField->modifiers & J9AccStatic) == J9AccStatic);
											memberName = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);

											if (NULL != fieldObj) {
												initImpl(currentThread, memberName, fieldObj);
											}
											if (VM_VMHelpers::exceptionPending(currentThread)) {
												goto done;
											}
										}
									}
									result += 1;
								}
								romField = romFieldsNextDo(&walkState);
							}
							currentITable = currentITable->next;
						}
					}
				} else if (J9_ARE_ANY_BITS_SET(matchFlags, MN_IS_CONSTRUCTOR | MN_IS_METHOD)) {
					UDATA classDepth = 0;
					if (J9_ARE_ANY_BITS_SET(matchFlags, MN_SEARCH_SUPERCLASSES)) {
						/* walk superclasses */
						J9CLASS_DEPTH(defClass);
					}
					J9Class *currentClass = defClass;

					while (NULL != currentClass) {
						if (!J9ROMCLASS_IS_PRIMITIVE_OR_ARRAY(currentClass->romClass)) {
							J9Method *currentMethod = currentClass->ramMethods;
							J9Method *endOfMethods = currentMethod + currentClass->romClass->romMethodCount;
							while (currentMethod != endOfMethods) {
								J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
								J9UTF8 *nameUTF = J9ROMMETHOD_SIGNATURE(romMethod);
								J9UTF8 *signatureUTF = J9ROMMETHOD_SIGNATURE(romMethod);

								if (((NULL == matchName) || J9UTF8_EQUALS(name, nameUTF))
								&& ((NULL == matchSig) || J9UTF8_EQUALS(sig, signatureUTF))
								) {
									if (skip > 0) {
										skip -=1;
									} else {
										if (index < length) {
											/* Refetch reference after GC point */
											resultsArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(results);
											j9object_t memberName = J9JAVAARRAYOFOBJECT_LOAD(currentThread, resultsArray, index);
											if (NULL == memberName) {
												result = -99;
												goto done;
											}
											PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, memberName);

											j9object_t methodObj = NULL;
											if (J9_ARE_NO_BITS_SET(romMethod->modifiers, J9AccStatic) && ('<' == (char)*J9UTF8_DATA(J9ROMMETHOD_NAME(romMethod)))) {
												/* create constructor object */
												methodObj = vm->reflectFunctions.createConstructorObject(currentMethod, currentClass, NULL, currentThread);
											} else {
												/* create method object */
												methodObj = vm->reflectFunctions.createMethodObject(currentMethod, currentClass, NULL, currentThread);
											}
											memberName = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);

											if (NULL != methodObj) {
												initImpl(currentThread, memberName, methodObj);
											}
											if (VM_VMHelpers::exceptionPending(currentThread)) {
												goto done;
											}
										}
									}
									result += 1;
								}
								currentMethod += 1;
							}
						}

						/* get the superclass */
						if (classDepth >= 1) {
							classDepth -= 1;
							currentClass = defClass->superclasses[classDepth];
						} else {
							currentClass = NULL;
						}
					}

					/* walk interfaces */
					if (J9_ARE_ANY_BITS_SET(matchFlags, MN_SEARCH_INTERFACES)) {
						J9ITable *currentITable = (J9ITable *)defClass->iTable;

						while (NULL != currentITable) {
							J9Class *currentClass = currentITable->interfaceClass;
							J9Method *currentMethod = currentClass->ramMethods;
							J9Method *endOfMethods = currentMethod + currentClass->romClass->romMethodCount;
							while (currentMethod != endOfMethods) {
								J9ROMMethod *romMethod = J9_ROM_METHOD_FROM_RAM_METHOD(currentMethod);
								J9UTF8 *nameUTF = J9ROMMETHOD_SIGNATURE(romMethod);
								J9UTF8 *signatureUTF = J9ROMMETHOD_SIGNATURE(romMethod);

								if (((NULL == matchName) || J9UTF8_EQUALS(name, nameUTF))
								&& ((NULL == matchSig) || J9UTF8_EQUALS(sig, signatureUTF))
								) {
									if (skip > 0) {
										skip -=1;
									} else {
										if (index < length) {
											/* Refetch reference after GC point */
											resultsArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(results);
											j9object_t memberName = J9JAVAARRAYOFOBJECT_LOAD(currentThread, resultsArray, index);
											if (NULL == memberName) {
												result = -99;
												goto done;
											}
											PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, memberName);

											j9object_t methodObj = NULL;
											if (J9_ARE_NO_BITS_SET(romMethod->modifiers, J9AccStatic) && ('<' == (char)*J9UTF8_DATA(J9ROMMETHOD_NAME(romMethod)))) {
												/* create constructor object */
												methodObj = vm->reflectFunctions.createConstructorObject(currentMethod, currentClass, NULL, currentThread);
											} else {
												/* create method object */
												methodObj = vm->reflectFunctions.createMethodObject(currentMethod, currentClass, NULL, currentThread);
											}
											memberName = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);

											if (NULL != methodObj) {
												initImpl(currentThread, memberName, methodObj);
											}
											if (VM_VMHelpers::exceptionPending(currentThread)) {
												goto done;
											}
										}
									}
									result += 1;
								}
								currentMethod += 1;
							}
							currentITable = currentITable->next;
						}
					}
				}
			}
		}
	}
done:
	j9mem_free_memory(name);
	j9mem_free_memory(sig);

	Trc_JCL_java_lang_invoke_MethodHandleNatives_getMembers_Exit(env, result);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

/**
 * static native long objectFieldOffset(MemberName self);  // e.g., returns vmindex
 * 
 * Returns the objectFieldOffset of the field represented by the MemberName
 * result should be same as if calling Unsafe.objectFieldOffset with the actual field object
 */
jlong JNICALL
Java_java_lang_invoke_MethodHandleNatives_objectFieldOffset(JNIEnv *env, jclass clazz, jobject self)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	jlong result = 0;

	Trc_JCL_java_lang_invoke_MethodHandleNatives_objectFieldOffset_Entry(env, self);

	if (NULL == self) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		j9object_t membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
		j9object_t clazzObject = J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, membernameObject);

		if (NULL == clazzObject) {
			vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
		} else {
			jint flags = J9VMJAVALANGINVOKEMEMBERNAME_FLAGS(currentThread, membernameObject);
			if (J9_ARE_ALL_BITS_SET(flags, MN_IS_FIELD) && J9_ARE_NO_BITS_SET(flags, J9AccStatic)) {
				J9JNIFieldID *fieldID = (J9JNIFieldID*)(UDATA)J9OBJECT_U64_LOAD(currentThread, membernameObject, vm->vmindexOffset);
				result = (jlong)fieldID->offset + J9VMTHREAD_OBJECT_HEADER_SIZE(currentThread);
			} else {
				vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
			}
		}
	}

	Trc_JCL_java_lang_invoke_MethodHandleNatives_objectFieldOffset_Exit(env, result);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

/**
 * static native long staticFieldOffset(MemberName self);  // e.g., returns vmindex
 * 
 * Returns the staticFieldOffset of the field represented by the MemberName
 * result should be same as if calling Unsafe.staticFieldOffset with the actual field object
 */
jlong JNICALL
Java_java_lang_invoke_MethodHandleNatives_staticFieldOffset(JNIEnv *env, jclass clazz, jobject self)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	jlong result = 0;

	Trc_JCL_java_lang_invoke_MethodHandleNatives_staticFieldOffset_Entry(env, self);

	if (NULL == self) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		j9object_t membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
		j9object_t clazzObject = J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, membernameObject);

		if (NULL == clazzObject) {
			vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
		} else {
			jint flags = J9VMJAVALANGINVOKEMEMBERNAME_FLAGS(currentThread, membernameObject);
			if (J9_ARE_ALL_BITS_SET(flags, MN_IS_FIELD & J9AccStatic)) {
				result = (jlong)(UDATA)J9OBJECT_U64_LOAD(currentThread, membernameObject, vm->vmtargetOffset);
			} else {
				vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
			}
		}
	}
	Trc_JCL_java_lang_invoke_MethodHandleNatives_staticFieldOffset_Exit(env, result);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

/**
 * static native Object staticFieldBase(MemberName self);  // e.g., returns clazz
 * 
 * Returns the staticFieldBase of the field represented by the MemberName
 * result should be same as if calling Unsafe.staticFieldBase with the actual field object
 */
jobject JNICALL
Java_java_lang_invoke_MethodHandleNatives_staticFieldBase(JNIEnv *env, jclass clazz, jobject self)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	const J9InternalVMFunctions *vmFuncs = currentThread->javaVM->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);
	jobject result = NULL;

	Trc_JCL_java_lang_invoke_MethodHandleNatives_staticFieldBase_Entry(env, self);
	if (NULL == self) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGNULLPOINTEREXCEPTION, NULL);
	} else {
		j9object_t membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
		j9object_t clazzObject = J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, membernameObject);

		if (NULL == clazzObject) {
			vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
		} else {
			jint flags = J9VMJAVALANGINVOKEMEMBERNAME_FLAGS(currentThread, membernameObject);
			if (J9_ARE_ALL_BITS_SET(flags, MN_IS_FIELD & J9AccStatic)) {
				result = vmFuncs->j9jni_createLocalRef(env, clazzObject);
			} else {
				vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
			}
		}
	}
	Trc_JCL_java_lang_invoke_MethodHandleNatives_staticFieldBase_Exit(env, result);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;
}

/**
 * static native Object getMemberVMInfo(MemberName self);  // returns {vmindex,vmtarget}
 * 
 * Return a 2-element java array containing the vm offset/target data
 * For a field MemberName, array contains:
 * 		(field offset, declaring class)
 * For a method MemberName, array contains:
 * 		(vtable index, MemberName object)
 */
jobject JNICALL
Java_java_lang_invoke_MethodHandleNatives_getMemberVMInfo(JNIEnv *env, jclass clazz, jobject self)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	jobject result = NULL;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	Trc_JCL_java_lang_invoke_MethodHandleNatives_getMemberVMInfo_Entry(env, self);
	if (NULL != self) {
		J9Class *arrayClass = fetchArrayClass(currentThread, J9VMJAVALANGOBJECT(vm));
		j9object_t arrayObject = vm->memoryManagerFunctions->J9AllocateIndexableObject(currentThread, arrayClass, 2, J9_GC_ALLOCATE_OBJECT_INSTRUMENTABLE);
		if (NULL == arrayObject) {
			vmFuncs->setHeapOutOfMemoryError(currentThread);
		} else {
			PUSH_OBJECT_IN_SPECIAL_FRAME(currentThread, arrayObject);
			j9object_t box = vm->memoryManagerFunctions->J9AllocateObject(currentThread, J9VMJAVALANGLONG_OR_NULL(vm), J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
			if (NULL == box) {
				/* Drop arrayObject */
				DROP_OBJECT_IN_SPECIAL_FRAME(currentThread);
				vmFuncs->setHeapOutOfMemoryError(currentThread);
			} else {
				arrayObject = POP_OBJECT_IN_SPECIAL_FRAME(currentThread);
				j9object_t membernameObject = J9_JNI_UNWRAP_REFERENCE(self);
				jint flags = J9VMJAVALANGINVOKEMEMBERNAME_FLAGS(currentThread, membernameObject);
				jlong vmindex = (jlong)(UDATA)J9OBJECT_U64_LOAD(currentThread, membernameObject, vm->vmindexOffset);
				j9object_t target = NULL;
				if (J9_ARE_ANY_BITS_SET(flags, MN_IS_FIELD)) {
					vmindex = ((J9JNIFieldID*)vmindex)->offset;
					target = J9VMJAVALANGINVOKEMEMBERNAME_CLAZZ(currentThread, membernameObject);
				} else {
					J9JNIMethodID *methodID = (J9JNIMethodID*)vmindex;
					if (J9_ARE_ANY_BITS_SET(methodID->vTableIndex, J9_JNI_MID_INTERFACE)) {
						vmindex = methodID->vTableIndex & ~J9_JNI_MID_INTERFACE;
					} else if (0 == methodID->vTableIndex) {
						vmindex = -1;
					} else {
						vmindex = methodID->vTableIndex;
					}
					target = membernameObject;
				}

				J9VMJAVALANGLONG_SET_VALUE(currentThread, box, vmindex);
				J9JAVAARRAYOFOBJECT_STORE(currentThread, arrayObject, 0, box);
				J9JAVAARRAYOFOBJECT_STORE(currentThread, arrayObject, 1, target);

				result = vmFuncs->j9jni_createLocalRef(env, arrayObject);
			}
		}
	}
	Trc_JCL_java_lang_invoke_MethodHandleNatives_getMemberVMInfo_Exit(env, result);
	vmFuncs->internalExitVMToJNI(currentThread);
	return result;

}

/**
 * static native void setCallSiteTargetNormal(CallSite site, MethodHandle target)
 */
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_setCallSiteTargetNormal(JNIEnv *env, jclass clazz, jobject callsite, jobject target)
{
	setCallSiteTargetImpl((J9VMThread*)env, callsite, target, false);
}

/**
 * static native void setCallSiteTargetVolatile(CallSite site, MethodHandle target);
 */
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_setCallSiteTargetVolatile(JNIEnv *env, jclass clazz, jobject callsite, jobject target)
{
	setCallSiteTargetImpl((J9VMThread*)env, callsite, target, true);
}

/**
 * static native void copyOutBootstrapArguments(Class<?> caller, int[] indexInfo,
												int start, int end,
												Object[] buf, int pos,
												boolean resolve,
												Object ifNotAvailable);
 */
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_copyOutBootstrapArguments(JNIEnv *env, jclass clazz, jclass caller, jintArray indexInfo, jint start, jint end, jobjectArray buf, jint pos, jboolean resolve, jobject ifNotAvailable)
{
	J9VMThread *currentThread = (J9VMThread*)env;
	J9JavaVM *vm = currentThread->javaVM;
	const J9InternalVMFunctions *vmFuncs = vm->internalVMFunctions;
	vmFuncs->internalEnterVMFromJNI(currentThread);

	if ((NULL == caller) || (NULL == indexInfo) || (NULL == buf)) {
		vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
	} else {
		J9Class *callerClass = J9VM_J9CLASS_FROM_JCLASS(currentThread, caller);
		j9array_t indexInfoArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(indexInfo);
		j9array_t bufferArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(buf);

		if ((J9INDEXABLEOBJECT_SIZE(currentThread, indexInfoArray) < 2)) {
			vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
		} else if (((start < -4) || (start > end) || (pos < 0)) || ((jint)J9INDEXABLEOBJECT_SIZE(currentThread, bufferArray) <= pos) || ((jint)J9INDEXABLEOBJECT_SIZE(currentThread, bufferArray) <= (pos + end - start))) {
			vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGLINKAGEERROR, NULL);
		} else {
			U_16 bsmArgCount = (U_16)J9JAVAARRAYOFINT_LOAD(currentThread, indexInfoArray, 0);
			U_16 cpIndex = (U_16)J9JAVAARRAYOFINT_LOAD(currentThread, indexInfoArray, 1);
			J9ROMClass *romClass = callerClass->romClass;
			U_32 * cpShapeDescription = J9ROMCLASS_CPSHAPEDESCRIPTION(romClass);
			if (J9_CP_TYPE(cpShapeDescription, cpIndex) == J9CPTYPE_CONSTANT_DYNAMIC) {
				J9ROMConstantDynamicRef *romConstantRef = (J9ROMConstantDynamicRef*)(J9_ROM_CP_FROM_ROM_CLASS(romClass) + cpIndex);
				J9SRP *callSiteData = (J9SRP *) J9ROMCLASS_CALLSITEDATA(romClass);
				U_16 *bsmIndices = (U_16 *) (callSiteData + romClass->callSiteCount);
				U_16 *bsmData = bsmIndices + romClass->callSiteCount;

				/* clear the J9DescriptionCpPrimitiveType flag with mask to get bsmIndex */
				U_32 bsmIndex = (romConstantRef->bsmIndexAndCpType >> J9DescriptionCpTypeShift) & J9DescriptionCpBsmIndexMask;
				J9ROMNameAndSignature* nameAndSig = SRP_PTR_GET(&romConstantRef->nameAndSignature, J9ROMNameAndSignature*);

				/* Walk bsmData - skip all bootstrap methods before bsmIndex */
				for (U_32 i = 0; i < bsmIndex; i++) {
					/* increment by size of bsm data plus header */
					bsmData += (bsmData[1] + 2);
				}

				U_16 bsmCPIndex = bsmData[0];
				U_16 argCount = bsmData[1];

				/* Check the argCount from indexInfo array matches actual value */
				if (bsmArgCount != argCount) {
					vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGINTERNALERROR, NULL);
					goto done;
				}

				bsmData += 2;

				while (start < end) {
					/* Copy the arguments between start and end to the buf array
					 *
					 * Negative start index refer to the mandatory arguments for a bootstrap method
					 * -4 -> Lookup
					 * -3 -> name (String)
					 * -2 -> signature (MethodType)
					 * -1 -> argCount of optional arguments
					 */
					j9object_t obj = NULL;
					if (start >= 0) {
						U_16 argIndex = bsmData[start];
						J9ConstantPool *ramConstantPool = J9_CP_FROM_CLASS(callerClass);
						obj = resolveRefToObject(currentThread, ramConstantPool, argIndex, (JNI_TRUE == resolve));
						if ((NULL == obj) && (JNI_TRUE != resolve) && (NULL != ifNotAvailable)) {
							obj = J9_JNI_UNWRAP_REFERENCE(ifNotAvailable);
						}
					} else if (start == -4) {
						obj = resolveRefToObject(currentThread, J9_CP_FROM_CLASS(callerClass), bsmCPIndex, true);
					} else if (start == -3) {
						J9UTF8 *name = J9ROMNAMEANDSIGNATURE_NAME(nameAndSig);
						obj = vm->memoryManagerFunctions->j9gc_createJavaLangString(currentThread, J9UTF8_DATA(name), (U_32)J9UTF8_LENGTH(name), J9_STR_INTERN);
					} else if (start == -2) {
						J9UTF8 *signature = J9ROMNAMEANDSIGNATURE_SIGNATURE(nameAndSig);
						/* Call VM Entry point to create the MethodType - Result is put into the
						* currentThread->returnValue as entry points don't "return" in the expected way
						*/
						vmFuncs->sendFromMethodDescriptorString(currentThread, signature, callerClass->classLoader, NULL);
						obj = (j9object_t)currentThread->returnValue;
					} else if (start == -1) {
						obj = vm->memoryManagerFunctions->J9AllocateObject(currentThread, J9VMJAVALANGINTEGER_OR_NULL(vm), J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
						if (NULL == obj) {
							vmFuncs->setHeapOutOfMemoryError(currentThread);
						} else {
							J9VMJAVALANGINTEGER_SET_VALUE(currentThread, obj, argCount);
						}
					}
					if (VM_VMHelpers::exceptionPending(currentThread)) {
						goto done;
					}
					/* Refetch reference after GC point */
					bufferArray = (j9array_t)J9_JNI_UNWRAP_REFERENCE(buf);
					J9JAVAARRAYOFOBJECT_STORE(currentThread, bufferArray, pos, obj);
					start += 1;
					pos += 1;
				}
			} else {
				vmFuncs->setCurrentExceptionUTF(currentThread, J9VMCONSTANTPOOL_JAVALANGLINKAGEERROR, NULL);
			}
		}
	}
done:
	vmFuncs->internalExitVMToJNI(currentThread);
}

/**
 * private static native void clearCallSiteContext(CallSiteContext context);
 */
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_clearCallSiteContext(JNIEnv *env, jclass clazz, jobject context)
{
	return;
}

/**
 * private static native int getNamedCon(int which, Object[] name);
 */
jint JNICALL
Java_java_lang_invoke_MethodHandleNatives_getNamedCon(JNIEnv *env, jclass clazz, jint which, jobjectArray name)
{
	return 0;
}

/**
 * private static native void registerNatives();
 */
void JNICALL
Java_java_lang_invoke_MethodHandleNatives_registerNatives(JNIEnv *env, jclass clazz)
{
	return;
}


} /* extern "C" */
