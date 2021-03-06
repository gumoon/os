/*++

Copyright (c) 2015 Minoca Corp.

    This file is licensed under the terms of the GNU General Public License
    version 3. Alternative licensing terms are available. Contact
    info@minocacorp.com for details. See the LICENSE file at the root of this
    project for complete licensing information.

Module Name:

    obj.c

Abstract:

    This module handles low level object manipulation for Chalk.

Author:

    Evan Green 14-Oct-2015

Environment:

    POSIX

--*/

//
// ------------------------------------------------------------------- Includes
//

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chalkp.h"

//
// ---------------------------------------------------------------- Definitions
//

//
// ------------------------------------------------------ Data Type Definitions
//

/*++

Structure Description:

    This structure stores the iterator context for a dictionary.

Members:

    Next - Stores a pointer to the next entry to return in an iteration.

    Generation - Stores the dictionary generation number when the iterator was
        created.

--*/

typedef struct _CHALK_DICT_ITERATOR {
    PCHALK_DICT_ENTRY Next;
    UINTN Generation;
} CHALK_DICT_ITERATOR, *PCHALK_DICT_ITERATOR;

//
// ----------------------------------------------- Internal Function Prototypes
//

VOID
ChalkGutObject (
    PCHALK_OBJECT Object
    );

VOID
ChalkDestroyList (
    PCHALK_OBJECT List
    );

VOID
ChalkDestroyDict (
    PCHALK_OBJECT Dict
    );

VOID
ChalkDestroyDictEntry (
    PCHALK_DICT_ENTRY Entry
    );

//
// -------------------------------------------------------------------- Globals
//

PSTR ChalkObjectTypeNames[ChalkObjectCount] = {
    "INVALID",
    "null",
    "integer",
    "string",
    "dict",
    "list",
    "function",
};

//
// The one and only null object. It starts with a single reference. If
// reference counting is done correctly, this will mean it is never destroyed.
//

CHALK_OBJECT_HEADER ChalkNull = {
    ChalkObjectNull,
    1
};

//
// ------------------------------------------------------------------ Functions
//

PCHALK_OBJECT
ChalkCreateNull (
    VOID
    )

/*++

Routine Description:

    This routine creates a new null object with an initial reference. Really it
    just returns the same object every time with an incremented reference, but
    the caller should not assume this.

Arguments:

    None.

Return Value:

    Returns a pointer to the new null object on success.

    NULL on allocation failure.

--*/

{

    ChalkObjectAddReference((PCHALK_OBJECT)&ChalkNull);
    return (PCHALK_OBJECT)&ChalkNull;
}

PCHALK_OBJECT
ChalkCreateInteger (
    LONGLONG Value
    )

/*++

Routine Description:

    This routine creates a new integer object.

Arguments:

    Value - Supplies the initial value.

Return Value:

    Returns a pointer to the new integer on success.

    NULL on allocation failure.

--*/

{

    PCHALK_INT Int;

    Int = ChalkAllocate(sizeof(CHALK_OBJECT));
    if (Int == NULL) {
        return NULL;
    }

    memset(Int, 0, sizeof(CHALK_OBJECT));
    Int->Header.Type = ChalkObjectInteger;
    Int->Header.ReferenceCount = 1;
    Int->Value = Value;
    return (PCHALK_OBJECT)Int;
}

PCHALK_OBJECT
ChalkCreateString (
    PSTR InitialValue,
    ULONG Size
    )

/*++

Routine Description:

    This routine creates a new string object.

Arguments:

    InitialValue - Supplies an optional pointer to the initial value.

    Size - Supplies the size of the initial value.

Return Value:

    Returns a pointer to the new string on success.

    NULL on allocation failure.

--*/

{

    ULONG AllocateSize;
    PCHALK_STRING String;

    String = ChalkAllocate(sizeof(CHALK_OBJECT));
    if (String == NULL) {
        return NULL;
    }

    memset(String, 0, sizeof(CHALK_OBJECT));
    AllocateSize = Size + 1;
    String->String = ChalkAllocate(AllocateSize);
    if (String->String == NULL) {
        ChalkFree(String);
        return NULL;
    }

    memcpy(String->String, InitialValue, Size);
    String->String[AllocateSize - 1] = '\0';
    String->Size = Size;
    String->Header.Type = ChalkObjectString;
    String->Header.ReferenceCount = 1;
    return (PCHALK_OBJECT)String;
}

INT
ChalkStringAdd (
    PCHALK_OBJECT Left,
    PCHALK_OBJECT Right,
    PCHALK_OBJECT *Result
    )

/*++

Routine Description:

    This routine adds two strings together, concatenating them.

Arguments:

    Left - Supplies a pointer to the left side of the operation.

    Right - Supplies a pointer to the right side of the operation.

    Result - Supplies a pointer where the result will be returned on success.

Return Value:

    0 on success.

    Returns an error number on catastrophic failure.

--*/

{

    ULONG Size;
    PCHALK_OBJECT String;

    assert((Left->Header.Type == ChalkObjectString) &&
           (Right->Header.Type == ChalkObjectString));

    String = ChalkCreateString(NULL, 0);
    if (String == NULL) {
        return ENOMEM;
    }

    ChalkFree(String->String.String);

    //
    // The size does not account for the null terminator.
    //

    Size = Left->String.Size + Right->String.Size;
    String->String.String = ChalkAllocate(Size + 1);
    if (String->String.String == NULL) {
        ChalkObjectReleaseReference(String);
        return ENOMEM;
    }

    memcpy(String->String.String, Left->String.String, Left->String.Size);
    memcpy(String->String.String + Left->String.Size,
           Right->String.String,
           Right->String.Size);

    String->String.String[Size] = '\0';
    String->String.Size = Size;
    *Result = String;
    return 0;
}

PCHALK_OBJECT
ChalkCreateList (
    PCHALK_OBJECT *InitialValues,
    ULONG Size
    )

/*++

Routine Description:

    This routine creates a new empty list object.

Arguments:

    InitialValues - Supplies an optional pointer to the initial values to set
        on the list.

    Size - Supplies the number of entries in the initial values array.

Return Value:

    Returns a pointer to the new string on success.

    NULL on allocation failure.

--*/

{

    ULONG Index;
    PCHALK_LIST List;

    List = ChalkAllocate(sizeof(CHALK_OBJECT));
    if (List == NULL) {
        return NULL;
    }

    memset(List, 0, sizeof(CHALK_OBJECT));
    List->Header.Type = ChalkObjectList;
    List->Header.ReferenceCount = 1;
    if (Size != 0) {
        List->Array = ChalkAllocate(Size * sizeof(PVOID));
        if (List->Array == NULL) {
            ChalkFree(List);
            return NULL;
        }

        List->Count = Size;
        if (InitialValues != NULL) {
            memcpy(List->Array, InitialValues, Size * sizeof(PVOID));
            for (Index = 0; Index < Size; Index += 1) {
                if (List->Array[Index] != NULL) {
                    ChalkObjectAddReference(List->Array[Index]);
                }
            }

        } else {
            memset(List->Array, 0, Size * sizeof(PVOID));
        }
    }

    return (PCHALK_OBJECT)List;
}

PCHALK_OBJECT
ChalkListLookup (
    PCHALK_OBJECT List,
    ULONG Index
    )

/*++

Routine Description:

    This routine looks up the value at a particular list index.

Arguments:

    List - Supplies a pointer to the list.

    Index - Supplies the index to lookup.

Return Value:

    Returns a pointer to the list element with an increased reference count on
    success.

    NULL if the object at that index does not exist.

--*/

{

    PCHALK_OBJECT Object;

    assert(List->Header.Type == ChalkObjectList);

    if (Index >= List->List.Count) {
        return NULL;
    }

    Object = List->List.Array[Index];
    if (Object != NULL) {
        ChalkObjectAddReference(Object);
    }

    return Object;
}

INT
ChalkListSetElement (
    PCHALK_OBJECT ListObject,
    ULONG Index,
    PCHALK_OBJECT Object
    )

/*++

Routine Description:

    This routine sets the given list index to the given object.

Arguments:

    ListObject - Supplies a pointer to the list.

    Index - Supplies the index to set.

    Object - Supplies a pointer to the object to set at that list index. The
        reference count on the object will be increased on success.

Return Value:

    0 on success.

    Returns an error number on allocation failure.

--*/

{

    PCHALK_LIST List;
    PVOID NewBuffer;

    assert(ListObject->Header.Type == ChalkObjectList);

    List = (PCHALK_LIST)ListObject;
    if (List->Count <= Index) {
        NewBuffer = ChalkReallocate(List->Array, sizeof(PVOID) * (Index + 1));
        if (NewBuffer == NULL) {
            return ENOMEM;
        }

        List->Array = NewBuffer;
        memset(&(List->Array[List->Count]),
               0,
               sizeof(PVOID) * (Index + 1 - List->Count));

        List->Count = Index + 1;
    }

    if (List->Array[Index] != NULL) {
        ChalkObjectReleaseReference(List->Array[Index]);
    }

    List->Array[Index] = Object;
    if (Object != NULL) {
        ChalkObjectAddReference(Object);
    }

    return 0;
}

INT
ChalkListAdd (
    PCHALK_OBJECT Destination,
    PCHALK_OBJECT Addition
    )

/*++

Routine Description:

    This routine adds two lists together, storing the result in the first.

Arguments:

    Destination - Supplies a pointer to the destination. The list elements will
        be added to this list.

    Addition - Supplies the list containing the elements to add.

Return Value:

    0 on success.

    Returns an error number on catastrophic failure.

--*/

{

    ULONG Index;
    PCHALK_LIST LeftList;
    PCHALK_OBJECT *NewArray;
    ULONG NewSize;
    PCHALK_LIST RightList;

    LeftList = (PCHALK_LIST)Destination;
    RightList = (PCHALK_LIST)Addition;

    assert((LeftList->Header.Type == ChalkObjectList) &&
           (RightList->Header.Type == ChalkObjectList));

    NewSize = LeftList->Count + RightList->Count;
    NewArray = ChalkReallocate(LeftList->Array, NewSize * sizeof(PVOID));
    if (NewArray == NULL) {
        return ENOMEM;
    }

    LeftList->Array = NewArray;
    for (Index = LeftList->Count; Index < NewSize; Index += 1) {
        NewArray[Index] = RightList->Array[Index - LeftList->Count];
        if (NewArray[Index] != NULL) {
            ChalkObjectAddReference(NewArray[Index]);
        }
    }

    LeftList->Count = NewSize;
    return 0;
}

INT
ChalkListInitializeIterator (
    PCHALK_OBJECT List,
    PVOID *Context
    )

/*++

Routine Description:

    This routine prepares to iterate over a list.

Arguments:

    List - Supplies a pointer to the list to iterate over.

    Context - Supplies a pointer where a pointer's worth of context will be
        returned, the contents of which are internal to the list structure.

Return Value:

    0 on success.

    Returns an error number on catastrophic failure.

--*/

{

    assert(List->Header.Type == ChalkObjectList);

    *Context = (PVOID)0;
    return 0;
}

INT
ChalkListIterate (
    PCHALK_OBJECT List,
    PVOID *Context,
    PCHALK_OBJECT *Iteration
    )

/*++

Routine Description:

    This routine retrieves the next value in a list iteration.

Arguments:

    List - Supplies a pointer to the list to iterate over.

    Context - Supplies a pointer to the iteration context. This will be updated
        to advance the iteration.

    Iteration - Supplies a pointer where the next value will be returned.
        Returns NULL when the end of the list is encountered. The reference
        count is NOT incremented on this object.

Return Value:

    0 on success.

    Returns an error number on catastrophic failure.

--*/

{

    ULONG Index;

    assert(List->Header.Type == ChalkObjectList);

    Index = (UINTN)*Context;
    while (Index < List->List.Count) {
        if (List->List.Array[Index] == NULL) {
            Index += 1;
            continue;
        }

        *Iteration = List->List.Array[Index];
        Index += 1;
        *Context = (PVOID)(UINTN)Index;
        return 0;
    }

    *Context = (PVOID)(UINTN)Index;
    *Iteration = NULL;
    return 0;
}

VOID
ChalkListDestroyIterator (
    PCHALK_OBJECT List,
    PVOID *Context
    )

/*++

Routine Description:

    This routine cleans up a list iterator.

Arguments:

    List - Supplies a pointer to the list that was being iterated over.

    Context - Supplies a pointer to the iterator's context pointer.

Return Value:

    None.

--*/

{

    *Context = NULL;
    return;
}

PCHALK_OBJECT
ChalkCreateDict (
    PCHALK_OBJECT Source
    )

/*++

Routine Description:

    This routine creates a new empty dictionary object.

Arguments:

    Source - Supplies an optional pointer to a dictionary to copy.

Return Value:

    Returns a pointer to the new string on success.

    NULL on allocation failure.

--*/

{

    PLIST_ENTRY CurrentEntry;
    PCHALK_OBJECT Dict;
    PCHALK_DICT_ENTRY Entry;
    INT Status;

    Dict = ChalkAllocate(sizeof(CHALK_OBJECT));
    if (Dict == NULL) {
        return NULL;
    }

    memset(Dict, 0, sizeof(CHALK_OBJECT));
    Dict->Header.Type = ChalkObjectDict;
    Dict->Header.ReferenceCount = 1;
    INITIALIZE_LIST_HEAD(&(Dict->Dict.EntryList));
    if (Source != NULL) {

        assert(Source->Header.Type == ChalkObjectDict);

        CurrentEntry = Source->Dict.EntryList.Next;
        while (CurrentEntry != &(Source->Dict.EntryList)) {
            Entry = LIST_VALUE(CurrentEntry, CHALK_DICT_ENTRY, ListEntry);
            CurrentEntry = CurrentEntry->Next;
            Status = ChalkDictSetElement(Dict, Entry->Key, Entry->Value, NULL);
            if (Status != 0) {
                ChalkObjectReleaseReference(Dict);
                return NULL;
            }
        }
    }

    return Dict;
}

INT
ChalkDictSetElement (
    PCHALK_OBJECT DictObject,
    PCHALK_OBJECT Key,
    PCHALK_OBJECT Value,
    PCHALK_OBJECT **LValue
    )

/*++

Routine Description:

    This routine adds or assigns a given value for a specific key.

Arguments:

    DictObject - Supplies a pointer to the dictionary.

    Key - Supplies a pointer to the key. This cannot be NULL. A reference will
        be added to the key if it is saved in the dictionary.

    Value - Supplies a pointer to the value. A reference will be added.

    LValue - Supplies an optional pointer where an LValue pointer will be
        returned on success. The caller can use the return of this pointer to
        assign into the dictionary element later.

Return Value:

    0 on success.

    Returns an error number on failure.

--*/

{

    PCHALK_DICT_ENTRY Entry;

    assert(DictObject->Header.Type == ChalkObjectDict);
    assert(Key != NULL);

    if ((Key->Header.Type != ChalkObjectInteger) &&
        (Key->Header.Type != ChalkObjectString)) {

        fprintf(stderr,
                "Cannot add type %s as dictionary key.\n",
                ChalkObjectTypeNames[Key->Header.Type]);

        return EINVAL;
    }

    Entry = ChalkDictLookup(DictObject, Key);

    //
    // If there is no entry, replace it.
    //

    if (Entry == NULL) {
        Entry = ChalkAllocate(sizeof(CHALK_DICT_ENTRY));
        if (Entry == NULL) {
            return ENOMEM;
        }

        memset(Entry, 0, sizeof(CHALK_DICT_ENTRY));
        Entry->Key = Key;
        ChalkObjectAddReference(Key);
        INSERT_BEFORE(&(Entry->ListEntry), &(DictObject->Dict.EntryList));
        DictObject->Dict.Generation += 1;
        DictObject->Dict.Count += 1;
    }

    ChalkObjectAddReference(Value);
    if (Entry->Value != NULL) {
        ChalkObjectReleaseReference(Entry->Value);
    }

    Entry->Value = Value;
    if (LValue != NULL) {
        *LValue = &(Entry->Value);
    }

    return 0;
}

PCHALK_DICT_ENTRY
ChalkDictLookup (
    PCHALK_OBJECT DictObject,
    PCHALK_OBJECT Key
    )

/*++

Routine Description:

    This routine attempts to find an entry in the given dictionary for a
    specific key.

Arguments:

    DictObject - Supplies a pointer to the dictionary to query.

    Key - Supplies a pointer to the key object to search.

Return Value:

    Returns a pointer to the dictionary entry on success.

    NULL if the key was not found.

--*/

{

    PLIST_ENTRY CurrentEntry;
    PCHALK_DICT Dict;
    PCHALK_DICT_ENTRY Entry;

    assert(DictObject->Header.Type == ChalkObjectDict);

    Dict = (PCHALK_DICT)DictObject;
    CurrentEntry = Dict->EntryList.Next;
    while (CurrentEntry != &(Dict->EntryList)) {
        Entry = LIST_VALUE(CurrentEntry, CHALK_DICT_ENTRY, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        if (ChalkCompareObjects(Entry->Key, Key) == 0) {
            return Entry;
        }
    }

    return NULL;
}

INT
ChalkDictAdd (
    PCHALK_OBJECT Destination,
    PCHALK_OBJECT Addition
    )

/*++

Routine Description:

    This routine adds two dictionaries together, returning the result in the
    left one.

Arguments:

    Destination - Supplies a pointer to the dictionary to add to.

    Addition - Supplies the entries to add.

Return Value:

    0 on success.

    Returns an error number on catastrophic failure.

--*/

{

    PLIST_ENTRY CurrentEntry;
    PCHALK_DICT_ENTRY Entry;
    INT Status;

    assert(Destination->Header.Type == ChalkObjectDict);
    assert(Addition->Header.Type == ChalkObjectDict);

    CurrentEntry = Addition->Dict.EntryList.Next;
    while (CurrentEntry != &(Addition->Dict.EntryList)) {
        Entry = LIST_VALUE(CurrentEntry, CHALK_DICT_ENTRY, ListEntry);
        CurrentEntry = CurrentEntry->Next;
        Status = ChalkDictSetElement(Destination,
                                     Entry->Key,
                                     Entry->Value,
                                     NULL);

        if (Status != 0) {
            return Status;
        }
    }

    return 0;
}

INT
ChalkDictInitializeIterator (
    PCHALK_OBJECT Dict,
    PVOID *Context
    )

/*++

Routine Description:

    This routine prepares to iterate over a dictionary.

Arguments:

    Dict - Supplies a pointer to the dictionary to iterate over.

    Context - Supplies a pointer where a pointer's worth of context will be
        returned, the contents of which are internal to the dict structure.

Return Value:

    0 on success.

    Returns an error number on catastrophic failure.

--*/

{

    PCHALK_DICT_ITERATOR Iterator;

    assert(Dict->Header.Type == ChalkObjectDict);

    Iterator = ChalkAllocate(sizeof(CHALK_DICT_ITERATOR));
    if (Iterator == NULL) {
        return ENOMEM;
    }

    memset(Iterator, 0, sizeof(CHALK_DICT_ITERATOR));
    if (!LIST_EMPTY(&(Dict->Dict.EntryList))) {
        Iterator->Next = LIST_VALUE(Dict->Dict.EntryList.Next,
                                    CHALK_DICT_ENTRY,
                                    ListEntry);

    } else {
        Iterator->Next = NULL;
    }

    Iterator->Generation = Dict->Dict.Generation;
    *Context = Iterator;
    return 0;
}

INT
ChalkDictIterate (
    PCHALK_OBJECT Dict,
    PVOID *Context,
    PCHALK_OBJECT *Iteration
    )

/*++

Routine Description:

    This routine retrieves the next value in a dictionary iteration.

Arguments:

    Dict - Supplies a pointer to the dict to iterate over.

    Context - Supplies a pointer to the iteration context. This will be updated
        to advance the iteration.

    Iteration - Supplies a pointer where the next value will be returned.
        Returns NULL when the end of the dict is encountered. The reference
        count is NOT incremented on this object.

Return Value:

    0 on success.

    Returns an error number on catastrophic failure.

--*/

{

    PCHALK_DICT_ENTRY Entry;
    PCHALK_DICT_ITERATOR Iterator;

    assert(Dict->Header.Type == ChalkObjectDict);

    Iterator = *Context;

    //
    // Detect dictionary changes during iteration, which might lead to a wild
    // pointer in the iteration structure.
    //

    if (Iterator->Generation != Dict->Dict.Generation) {
        fprintf(stderr, "Error: Dictionary changed while iterating.\n");
        return ERANGE;
    }

    if (Iterator->Next == NULL) {
        *Iteration = NULL;

    } else {
        Entry = Iterator->Next;
        *Iteration = Entry->Key;
        if (Entry->ListEntry.Next == &(Dict->Dict.EntryList)) {
            Entry = NULL;

        } else {
            Entry = LIST_VALUE(Entry->ListEntry.Next,
                               CHALK_DICT_ENTRY,
                               ListEntry);
        }

        Iterator->Next = Entry;
    }

    return 0;
}

VOID
ChalkDictDestroyIterator (
    PCHALK_OBJECT Dict,
    PVOID *Context
    )

/*++

Routine Description:

    This routine cleans up a dictionary iterator.

Arguments:

    Dict - Supplies a pointer to the dictionary that was being iterated over.

    Context - Supplies a pointer to the iterator's context pointer.

Return Value:

    None.

--*/

{

    if (*Context != NULL) {
        ChalkFree(*Context);
        *Context = NULL;
    }

    return;
}

PCHALK_OBJECT
ChalkCreateFunction (
    PCHALK_OBJECT Arguments,
    PVOID Body,
    PCHALK_SCRIPT Script
    )

/*++

Routine Description:

    This routine creates a new function object.

Arguments:

    Arguments - Supplies a pointer to a list containing the arguments for the
        function. A reference is added, and this list is used directly.

    Body - Supplies a pointer to the Abstract Syntax Tree node representing the
        body of the function (what to execute when the function is called).
        This is opaque, but is currently of type PPARSER_NODE.

    Script - Supplies a pointer to the script the function is defined in.

Return Value:

    Returns a pointer to the new object on success.

    NULL on failure.

--*/

{

    PCHALK_OBJECT Function;

    Function = ChalkAllocate(sizeof(CHALK_OBJECT));
    if (Function == NULL) {
        return NULL;
    }

    memset(Function, 0, sizeof(CHALK_OBJECT));
    Function->Header.Type = ChalkObjectFunction;
    Function->Header.ReferenceCount = 1;
    Function->Function.Arguments = Arguments;
    if (Arguments != NULL) {
        ChalkObjectAddReference(Arguments);
    }

    Function->Function.Body = Body;
    Function->Function.Script = Script;
    return Function;
}

PCHALK_OBJECT
ChalkObjectCopy (
    PCHALK_OBJECT Source
    )

/*++

Routine Description:

    This routine creates a deep copy of the given object.

Arguments:

    Source - Supplies the source to copy from.

Return Value:

    Returns a pointer to the new object on success.

    NULL on failure.

--*/

{

    PCHALK_OBJECT NewObject;
    PCHALK_OBJECT Object;

    Object = Source;
    switch (Object->Header.Type) {
    case ChalkObjectNull:
        NewObject = ChalkCreateNull();
        break;

    case ChalkObjectInteger:
        NewObject = ChalkCreateInteger(Object->Integer.Value);
        break;

    case ChalkObjectString:
        NewObject = ChalkCreateString(Object->String.String,
                                      Object->String.Size);

        break;

    case ChalkObjectList:
        NewObject = ChalkCreateList(Object->List.Array, Object->List.Count);
        break;

    case ChalkObjectDict:
        NewObject = ChalkCreateDict(Object);
        break;

    case ChalkObjectFunction:
        NewObject = ChalkCreateFunction(Object->Function.Arguments,
                                        Object->Function.Body,
                                        Object->Function.Script);

        break;

    default:

        assert(FALSE);

        NewObject = NULL;
    }

    return NewObject;
}

BOOL
ChalkObjectGetBooleanValue (
    PCHALK_OBJECT Object
    )

/*++

Routine Description:

    This routine converts an object to a boolean value.

Arguments:

    Object - Supplies a pointer to the object to booleanize.

Return Value:

    TRUE if the object is non-zero or non-empty.

    FALSE if the object is zero or empty.

--*/

{

    BOOL Result;

    switch (Object->Header.Type) {
    case ChalkObjectNull:
        Result = FALSE;
        break;

    case ChalkObjectInteger:
        Result = (Object->Integer.Value != 0);
        break;

    case ChalkObjectString:
        Result = (Object->String.Size != 0);
        break;

    case ChalkObjectList:
        Result = (Object->List.Count != 0);
        break;

    case ChalkObjectDict:
        Result = !LIST_EMPTY(&(Object->Dict.EntryList));
        break;

    case ChalkObjectFunction:
        Result = TRUE;
        break;

    default:

        assert(FALSE);

        Result = FALSE;
    }

    return Result;
}

VOID
ChalkObjectAddReference (
    PCHALK_OBJECT Object
    )

/*++

Routine Description:

    This routine adds a reference to the given Chalk object.

Arguments:

    Object - Supplies a pointer to the object to add a reference to.

Return Value:

    None.

--*/

{

    PCHALK_OBJECT_HEADER Header;

    Header = &(Object->Header);

    assert(Header->Type != ChalkObjectInvalid);
    assert((Header->ReferenceCount != 0) &&
           (Header->ReferenceCount < 0x10000000));

    Header->ReferenceCount += 1;
    return;
}

VOID
ChalkObjectReleaseReference (
    PCHALK_OBJECT Object
    )

/*++

Routine Description:

    This routine releases a reference from the given Chalk object. If the
    reference count its zero, the object is destroyed.

Arguments:

    Object - Supplies a pointer to the object to release.

Return Value:

    None.

--*/

{

    PCHALK_OBJECT_HEADER Header;

    Header = &(Object->Header);

    assert(Header->Type != ChalkObjectInvalid);
    assert((Header->ReferenceCount != 0) &&
           (Header->ReferenceCount < 0x10000000));

    Header->ReferenceCount -= 1;
    if (Header->ReferenceCount == 0) {
        ChalkGutObject(Object);
        ChalkFree(Header);
    }

    return;
}

INT
ChalkFunctionPrint (
    PCHALK_INTERPRETER Interpreter,
    PVOID Context,
    PCHALK_OBJECT *ReturnValue
    )

/*++

Routine Description:

    This routine implements the built in print function in the Chalk
    interpreter.

Arguments:

    Interpreter - Supplies a pointer to the interpreter context.

    Context - Supplies a pointer's worth of context given when the function
        was registered.

    ReturnValue - Supplies a pointer where a pointer to the return value will
        be returned.

Return Value:

    0 on success.

    Returns an error number on execution failure.

--*/

{

    PCHALK_OBJECT Element;
    BOOL First;
    UINTN Index;
    PCHALK_OBJECT Object;

    Object = ChalkCGetVariable(Interpreter, "object");

    assert(Object != NULL);

    //
    // If it's a list, print the contents separated by spaces.
    //

    if (Object->Header.Type == ChalkObjectList) {
        First = TRUE;
        for (Index = 0; Index < Object->List.Count; Index += 1) {
            Element = Object->List.Array[Index];
            if (Element == NULL) {
                continue;
            }

            if (First == FALSE) {
                printf(" ");

            } else {
                First = FALSE;
            }

            ChalkPrintObject(stdout, Element, 0);
        }

    } else {
        ChalkPrintObject(stdout, Object, 0);
    }

    return 0;
}

INT
ChalkFunctionLength (
    PCHALK_INTERPRETER Interpreter,
    PVOID Context,
    PCHALK_OBJECT *ReturnValue
    )

/*++

Routine Description:

    This routine implements the built in len function.

Arguments:

    Interpreter - Supplies a pointer to the interpreter context.

    Context - Supplies a pointer's worth of context given when the function
        was registered.

    ReturnValue - Supplies a pointer where a pointer to the return value will
        be returned.

Return Value:

    0 on success.

    Returns an error number on execution failure.

--*/

{

    PCHALK_OBJECT Object;
    UINTN Value;

    Object = ChalkCGetVariable(Interpreter, "object");

    assert(Object != NULL);

    Value = 0;
    switch (Object->Header.Type) {
    case ChalkObjectString:
        Value = strlen(Object->String.String);
        break;

    case ChalkObjectList:
        Value = Object->List.Count;
        break;

    case ChalkObjectDict:
        Value = Object->Dict.Count;
        break;

    default:
        Value = 0;
        break;
    }

    *ReturnValue = ChalkCreateInteger(Value);
    if (*ReturnValue == NULL) {
        return ENOMEM;
    }

    return 0;
}

INT
ChalkFunctionGet (
    PCHALK_INTERPRETER Interpreter,
    PVOID Context,
    PCHALK_OBJECT *ReturnValue
    )

/*++

Routine Description:

    This routine implements the built in get function, which returns the
    value at a dictionary key or null if the dictionary key doesn't exist.

Arguments:

    Interpreter - Supplies a pointer to the interpreter context.

    Context - Supplies a pointer's worth of context given when the function
        was registered.

    ReturnValue - Supplies a pointer where a pointer to the return value will
        be returned.

Return Value:

    0 on success.

    Returns an error number on execution failure.

--*/

{

    PCHALK_DICT_ENTRY Entry;
    PCHALK_OBJECT Key;
    PCHALK_OBJECT Object;
    PCHALK_OBJECT Value;

    Object = ChalkCGetVariable(Interpreter, "object");
    Key = ChalkCGetVariable(Interpreter, "key");
    Value = NULL;

    assert((Object != NULL) && (Key != NULL));

    if ((Object->Header.Type != ChalkObjectDict) &&
        (Object->Header.Type != ChalkObjectNull)) {

        fprintf(stderr, "Error: get() passed non-dictionary object\n");
        return EINVAL;
    }

    if (Object->Header.Type == ChalkObjectDict) {
        Entry = ChalkDictLookup(Object, Key);
        if (Entry != NULL) {
            Value = Entry->Value;
            ChalkObjectAddReference(Value);
        }
    }

    if (Value == NULL) {
        Value = ChalkCreateNull();
    }

    *ReturnValue = Value;
    return 0;
}

INT
ChalkCompareObjects (
    PCHALK_OBJECT Left,
    PCHALK_OBJECT Right
    )

/*++

Routine Description:

    This routine compares two objects.

Arguments:

    Interpreter - Supplies a pointer to the interpreter.

    Left - Supplies a pointer to the left side of the operation.

    Right - Supplies a pointer to the right side of the operation. This is
        ignored for unary operators.

    Operator - Supplies the operation to perform.

    Result - Supplies a pointer where the result will be returned on success.

Return Value:

    -1 if Left < Right.

    0 if Left == Right.

    1 if Left > Right.

--*/

{

    INT Index;
    INT Result;

    //
    // If the types are not equal, then compare based on the types.
    //

    if (Left->Header.Type != Right->Header.Type) {
        if (Left->Header.Type < Right->Header.Type) {
            return -1;
        }

        return 1;
    }

    switch (Left->Header.Type) {
    case ChalkObjectNull:
        Result = 0;
        break;

    case ChalkObjectInteger:
        if (Left->Integer.Value < Right->Integer.Value) {
            Result = -1;

        } else if (Left->Integer.Value > Right->Integer.Value) {
            Result = 1;

        } else {
            Result = 0;
        }

        break;

    case ChalkObjectString:
        Result = strcmp(Left->String.String, Right->String.String);
        if (Result < 0) {
            Result = -1;

        } else if (Result > 0) {
            Result = 1;
        }

        break;

    case ChalkObjectList:
        if (Left->List.Count < Right->List.Count) {
            Result = -1;

        } else if (Left->List.Count > Right->List.Count) {
            Result = 1;

        } else {
            Result = 0;
            for (Index = 0; Index < Left->List.Count; Index += 1) {
                Result = ChalkCompareObjects(Left->List.Array[Index],
                                             Right->List.Array[Index]);

                if (Result != 0) {
                    break;
                }
            }
        }

        break;

    //
    // For any other objects, just compare pointer values.
    //

    default:
        if ((UINTN)Left < (UINTN)Right) {
            Result = -1;

        } else if ((UINTN)Left > (UINTN)Right) {
            Result = 1;

        } else {
            Result = 0;
        }

        break;
    }

    return Result;
}

VOID
ChalkPrintObject (
    FILE *File,
    PCHALK_OBJECT Object,
    ULONG RecursionDepth
    )

/*++

Routine Description:

    This routine prints an object.

Arguments:

    File - Supplies a pointer to the file to print to.

    Object - Supplies a pointer to the object to print.

    RecursionDepth - Supplies the recursion depth.

Return Value:

    None.

--*/

{

    PCHALK_OBJECT *Array;
    ULONG Count;
    PLIST_ENTRY CurrentEntry;
    PCHALK_DICT_ENTRY Entry;
    ULONG Index;
    ULONG ReferenceCount;
    ULONG Size;
    PSTR String;
    CHALK_OBJECT_TYPE Type;

    if (Object == NULL) {
        fprintf(File, "0");
        return;
    }

    Type = Object->Header.Type;

    //
    // Avoid infinite recursion.
    //

    if (Object->Header.ReferenceCount == (ULONG)-1) {
        if (Type == ChalkObjectList) {
            fprintf(File, "[...]");

        } else {

            assert(Type == ChalkObjectDict);

            fprintf(File, "{...}");
        }

        return;
    }

    //
    // Trash the reference count as a visit marker.
    //

    ReferenceCount = Object->Header.ReferenceCount;
    Object->Header.ReferenceCount = (ULONG)-1;
    switch (Type) {
    case ChalkObjectNull:
        fprintf(File, "null");
        break;

    case ChalkObjectInteger:
        fprintf(File, "%lld", Object->Integer.Value);
        break;

    case ChalkObjectString:
        if (RecursionDepth == 0) {
            fprintf(File, "%s", Object->String.String);
            break;
        }

        if (Object->String.Size == 0) {
            fprintf(File, "\"\"");

        } else {
            String = Object->String.String;
            Size = Object->String.Size;
            fprintf(File, "\"");
            while (Size != 0) {
                switch (*String) {
                case '\r':
                    fprintf(File, "\\r");
                    break;

                case '\n':
                    fprintf(File, "\\n");
                    break;

                case '\v':
                    fprintf(File, "\\v");
                    break;

                case '\t':
                    fprintf(File, "\\t");
                    break;

                case '\f':
                    fprintf(File, "\\f");
                    break;

                case '\b':
                    fprintf(File, "\\b");
                    break;

                case '\a':
                    fprintf(File, "\\a");
                    break;

                case '\\':
                    fprintf(File, "\\\\");
                    break;

                case '"':
                    fprintf(File, "\\\"");
                    break;

                default:
                    if ((*String < ' ') || ((UCHAR)*String >= 0x80)) {
                        fprintf(File, "\\x%02X", (UCHAR)*String);

                    } else {
                        fprintf(File, "%c", *String);
                    }

                    break;
                }

                String += 1;
                Size -= 1;
            }

            fprintf(File, "\"");
        }

        break;

    case ChalkObjectList:
        fprintf(File, "[");
        Array = Object->List.Array;
        Count = Object->List.Count;
        for (Index = 0; Index < Count; Index += 1) {
            ChalkPrintObject(File, Array[Index], RecursionDepth + 1);
            if (Index < Count - 1) {
                fprintf(File, ", ");
                if (Count >= 5) {
                    fprintf(File, "\n%*s", RecursionDepth + 1, "");
                }
            }
        }

        fprintf(File, "]");
        break;

    case ChalkObjectDict:
        fprintf(File, "{");
        CurrentEntry = Object->Dict.EntryList.Next;
        while (CurrentEntry != &(Object->Dict.EntryList)) {
            Entry = LIST_VALUE(CurrentEntry, CHALK_DICT_ENTRY, ListEntry);
            CurrentEntry = CurrentEntry->Next;
            ChalkPrintObject(File, Entry->Key, RecursionDepth + 1);
            fprintf(File, " : ");
            ChalkPrintObject(File, Entry->Value, RecursionDepth + 1);
            if (CurrentEntry != &(Object->Dict.EntryList)) {
                fprintf(File, "\n%*s", RecursionDepth + 1, "");
            }
        }

        fprintf(File, "}");
        break;

    case ChalkObjectFunction:
        fprintf(File, "Function at %p", Object->Function.Body);
        break;

    default:

        assert(FALSE);

        break;
    }

    Object->Header.ReferenceCount = ReferenceCount;
    return;
}

//
// --------------------------------------------------------- Internal Functions
//

VOID
ChalkGutObject (
    PCHALK_OBJECT Object
    )

/*++

Routine Description:

    This routine destroys the inner contents of the object.

Arguments:

    Object - Supplies a pointer to the object to release.

Return Value:

    None.

--*/

{

    switch (Object->Header.Type) {
    case ChalkObjectNull:
        fprintf(stderr, "Error: Reference counting problem on null object.\n");

        assert(FALSE);

        break;

    case ChalkObjectInteger:
        break;

    case ChalkObjectString:
        if (Object->String.String != NULL) {
            ChalkFree(Object->String.String);
            Object->String.String = NULL;
        }

        break;

    case ChalkObjectList:
        ChalkDestroyList(Object);
        break;

    case ChalkObjectDict:
        ChalkDestroyDict(Object);
        break;

    case ChalkObjectFunction:
        if (Object->Function.Arguments != NULL) {
            ChalkObjectReleaseReference(Object->Function.Arguments);
            Object->Function.Arguments = NULL;
        }

        Object->Function.Body = NULL;
        Object->Function.Script = NULL;
        break;

    default:

        assert(FALSE);

        break;
    }

    Object->Header.Type = ChalkObjectInvalid;
    return;
}

VOID
ChalkDestroyList (
    PCHALK_OBJECT List
    )

/*++

Routine Description:

    This routine destroys a Chalk list object.

Arguments:

    List - Supplies a pointer to the list.

Return Value:

    None.

--*/

{

    PCHALK_OBJECT *Array;
    ULONG Index;

    Array = List->List.Array;
    for (Index = 0; Index < List->List.Count; Index += 1) {
        if (Array[Index] != NULL) {
            ChalkObjectReleaseReference(Array[Index]);
        }
    }

    if (Array != NULL) {
        ChalkFree(Array);
    }

    return;
}

VOID
ChalkDestroyDict (
    PCHALK_OBJECT Dict
    )

/*++

Routine Description:

    This routine destroys a Chalk dictionary object.

Arguments:

    Dict - Supplies a pointer to the dictionary.

Return Value:

    None.

--*/

{

    PCHALK_DICT_ENTRY Entry;

    while (!LIST_EMPTY(&(Dict->Dict.EntryList))) {
        Entry = LIST_VALUE(Dict->Dict.EntryList.Next,
                           CHALK_DICT_ENTRY,
                           ListEntry);

        ChalkDestroyDictEntry(Entry);
        Dict->Dict.Count -= 1;
    }

    assert(Dict->Dict.Count == 0);

    return;
}

VOID
ChalkDestroyDictEntry (
    PCHALK_DICT_ENTRY Entry
    )

/*++

Routine Description:

    This routine removes and destroys a Chalk dictionary entry.

Arguments:

    Entry - Supplies a pointer to the dictionary entry.

Return Value:

    None.

--*/

{

    if (Entry->ListEntry.Next != NULL) {
        LIST_REMOVE(&(Entry->ListEntry));
        Entry->ListEntry.Next = NULL;
    }

    ChalkObjectReleaseReference(Entry->Key);
    if (Entry->Value != NULL) {
        ChalkObjectReleaseReference(Entry->Value);
    }

    ChalkFree(Entry);
    return;
}

