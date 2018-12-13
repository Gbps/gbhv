#pragma once
#include "extern.h"

BOOL HvUtilBitIsSet(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitSetBit(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitClearBit(SIZE_T BitField, SIZE_T BitPosition);

SIZE_T HvUtilBitGetBitRange(SIZE_T BitField, SIZE_T BitMax, SIZE_T BitMin);

SIZE_T HvUtilEncodeMustBeBits(SIZE_T DesiredValue, SIZE_T ControlMSR);

VOID HvUtilLog(LPCSTR MessageFormat, ...);

VOID HvUtilLogDebug(LPCSTR MessageFormat, ...);

VOID HvUtilLogSuccess(LPCSTR MessageFormat, ...);

VOID HvUtilLogError(LPCSTR MessageFormat, ...);

/**
 * Linked list for-each macro for traversing LIST_ENTRY structures.
 * 
 * _LISTHEAD_ is a pointer to the struct that the list head belongs to.
 * _LISTHEAD_NAME_ is the name of the variable which contains the list head. Should match the same name as the list entry struct member in the actual record.
 * _TARGET_TYPE_ is the type name of the struct of each item in the list
 * _TARGET_NAME_ is the name which will contain the pointer to the item each iteration
 * 
 * Example:
 * FOR_EACH_LIST_ENTRY(ProcessorContext->EptPageTable, DynamicSplitList, VMM_EPT_DYNAMIC_SPLIT, Split)
 * 		OsFreeNonpagedMemory(Split);
 * }
 * 
 * ProcessorContext->EptPageTable->DynamicSplitList is the head of the list.
 * VMM_EPT_DYNAMIC_SPLIT is the struct of each item in the list.
 * Split is the name of the local variable which will hold the pointer to the item.
 */
#define FOR_EACH_LIST_ENTRY(_LISTHEAD_, _LISTHEAD_NAME_, _TARGET_TYPE_, _TARGET_NAME_) \
	for (PLIST_ENTRY Entry = _LISTHEAD_->_LISTHEAD_NAME_.Flink; Entry != &_LISTHEAD_->_LISTHEAD_NAME_; Entry = Entry->Flink) { \
	P##_TARGET_TYPE_ _TARGET_NAME_ = CONTAINING_RECORD(Entry, _TARGET_TYPE_, _LISTHEAD_NAME_);

/**
 * The braces for the block are messy due to the need to define a local variable in the for loop scope.
 * Therefore, this macro just ends the for each block without messing up code editors trying to detect
 * the block indent level.
 */
# define FOR_EACH_LIST_ENTRY_END() }