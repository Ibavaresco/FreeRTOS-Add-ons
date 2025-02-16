/*============================================================================*/
/*
SimpleRTOS - Very simple RTOS for Microcontrollers
v2.00 (2014-01-21)
isaacbavaresco@yahoo.com.br
*/
/*============================================================================*/
/*
 Copyright (c) 2007-2014, Isaac Marino Bavaresco
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Neither the name of the author nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*============================================================================*/
#include <string.h>
#include "FreeRTOS.h"
#include "list.h"
#include "task.h"
/*============================================================================*/
#include "FlexiQueue.h"
/*============================================================================*/
flexiqueue_t *xFlexiQueueCreate( unsigned int QueueLength, int Mode )
	{
	flexiqueue_t	*Queue;
	char			*QueueBuffer;
	
	Queue		= pvPortMalloc( sizeof( flexiqueue_t ));
	if( Queue == NULL )
		return NULL;

	QueueBuffer	= pvPortMalloc( QueueLength );
	if( QueueBuffer == NULL )
		{
		vPortFree( Queue );
		return NULL;
		}

#if			defined QUEUE_STRICT_CHRONOLOGY
	Queue->ReadingOwner			= NULL;
	Queue->WritingOwner			= NULL;
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
	vListInitialise( &( Queue->TasksWaitingToWrite ) );
	vListInitialise( &( Queue->TasksWaitingToRead ) );
	Queue->QueueLength			= QueueLength;
	Queue->BytesFree			= QueueLength;
	Queue->QueueBuffer			= (unsigned char*)QueueBuffer;
	Queue->ItemsAvailable		= 0;
	Queue->RemoveIndex			= 0;
	Queue->InsertIndex			= 0;
	Queue->Mode					= Mode;

	return Queue;
	}
/*============================================================================*/
static inline __attribute((always_inline)) unsigned int EffectiveSize( unsigned int s )
	{
	return s + ( s > 128 ? 2 : 1 );
	}
/*============================================================================*/
static inline __attribute((always_inline)) unsigned int GetSizeOfNextItem( flexiqueue_t *q )
	{
	unsigned int	RemoveIndex, ItemLength;

	if( q->ItemsAvailable == 0 )
		return 0;

	RemoveIndex	= q->RemoveIndex;
	ItemLength	= (unsigned short)q->QueueBuffer[ RemoveIndex ];
	if( ++RemoveIndex >= q->QueueLength )
		RemoveIndex	= 0;
	if( ItemLength & 0x80 )
		{
		ItemLength	= ( ItemLength & 0x7f ) | ( (unsigned short)q->QueueBuffer[ RemoveIndex ] << 7 );
/*
		if( ++RemoveIndex >= q->QueueLength )
			RemoveIndex	= 0;
*/
		}
	return ItemLength + 1;
	}
/*============================================================================*/
int xFlexiQueueRead( flexiqueue_t *Queue, void *Ptr, unsigned int BufferSize, portTickType TimeToWait )
	{
	portTickType 		DeadLine;
	xTaskHandle			p;
	int					MustYield = 0;
	unsigned int		RemoveIndex, ItemLength, Aux, RemainingBytes;

	if( Queue == NULL )
		return 0;

	portENTER_CRITICAL();

#if			defined QUEUE_STRICT_CHRONOLOGY
	if( Queue->ItemsAvailable == 0 || Queue->ReadingOwner != NULL || !listLIST_IS_EMPTY( &Queue->TasksWaitingToRead ))
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
	if( Queue->ItemsAvailable == 0 )
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
		{
		if( TimeToWait == 0 )
			{
			portEXIT_CRITICAL();
			return 0;
			}

		DeadLine	= xTaskGetTickCount() + TimeToWait;
		vSetExtraParameter( xTaskGetCurrentTaskHandle(), (void*)BufferSize );

#if			!defined QUEUE_STRICT_CHRONOLOGY
		do
#endif	/*	!defined QUEUE_STRICT_CHRONOLOGY */
			{
			vTaskPlaceOnEventList( &( Queue->TasksWaitingToRead ), DeadLine );

	        taskYIELD();
	        }
#if			!defined QUEUE_STRICT_CHRONOLOGY
		while(( (signed long)TimeToWait < 0 || (signed long)( DeadLine - xTaskGetTickCount() ) > 0 ) && Queue->ItemsAvailable == 0 );
#endif	/*	!defined QUEUE_STRICT_CHRONOLOGY */

#if			defined QUEUE_STRICT_CHRONOLOGY
		if( Queue->ItemsAvailable == 0 || Queue->ReadingOwner != xTaskGetCurrentTaskHandle() )
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
		if( Queue->ItemsAvailable == 0 )
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
			{
			portEXIT_CRITICAL();
			return 0;
			}
		}

	RemoveIndex	= Queue->RemoveIndex;
	ItemLength	= (unsigned short)Queue->QueueBuffer[ RemoveIndex ];
	if( ++RemoveIndex >= Queue->QueueLength )
		RemoveIndex	= 0;
	if( ItemLength > 127 )
		{
		ItemLength	= ( ItemLength & 0x7f ) | ( (unsigned short)Queue->QueueBuffer[ RemoveIndex ] << 7 );
		if( ++RemoveIndex >= Queue->QueueLength )
			RemoveIndex	= 0;
		}
	ItemLength++;

	if( BufferSize < ItemLength )
		{
		portEXIT_CRITICAL();
		return -1;
		}

	RemainingBytes	= ItemLength;
	while(( Aux = min( RemainingBytes, Queue->QueueLength - RemoveIndex ) ) > 0 )
		{
		memcpy( (void*)Ptr, (void*)&Queue->QueueBuffer[ RemoveIndex ], Aux );
		Ptr	= (char*)Ptr + Aux;
		if(( RemoveIndex += Aux ) >= Queue->QueueLength )
			RemoveIndex	= 0;
		RemainingBytes	-= Aux;
		}

	Queue->RemoveIndex	= RemoveIndex;
	
	Queue->ItemsAvailable--;
	Queue->BytesFree	+= EffectiveSize( ItemLength );

#if			defined QUEUE_STRICT_CHRONOLOGY
	Queue->ReadingOwner		= NULL;
	/*------------------------------------------------------------------------*/
	/*
	 We got our item, now check to see whether there are more items and tasks
	 wanting them. This will set up a chain reaction.
	*/
	/*------------------------------------------------------------------------*/
	if( Queue->ItemsAvailable != 0 && ( p = (xTaskHandle)listGET_OWNER_OF_HEAD_ENTRY( &Queue->TasksWaitingToRead )) != NULL )
		{
/*@@@@
		for( Aux = GetSizeOfNextItem( Queue ); p != NULL && (unsigned int)pvGetExtraParameter( p ) < Aux; p = p->Next )
			{}
		if( p != NULL )
@@@@*/
			{
			Queue->ReadingOwner	= p;

			if( xTaskRemoveFromEventList( &Queue->TasksWaitingToRead ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IMMEDIATE ))
				MustYield	= 1;

			}
		}
	/*------------------------------------------------------------------------*/
	/*
	 We removed some bytes from the buffer, there should be room for more items
	 in the queue, check to see whether there is a task wanting to write and if
	 its item will fit in the buffer.
	*/
	/*------------------------------------------------------------------------*/
	if( Queue->WritingOwner == NULL && ( p = (xTaskHandle)listGET_OWNER_OF_HEAD_ENTRY( &Queue->TasksWaitingToWrite )) != NULL
		&& EffectiveSize( (unsigned int)pvGetExtraParameter( p )) <= Queue->BytesFree )
		{
		Queue->WritingOwner	= p;
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
	if( !listLIST_IS_EMPTY( &Queue->TasksWaitingToWrite ))
		{
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */

		if( xTaskRemoveFromEventList( &Queue->TasksWaitingToWrite ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IMMEDIATE ))
			MustYield	= 1;
		}

	if( MustYield )
		taskYIELD();
	portEXIT_CRITICAL();

	return ItemLength;
	}
/*============================================================================*/
int xFlexiQueueReadFromISR( flexiqueue_t *Queue, void *Ptr, unsigned int BufferSize )
	{
	unsigned int	RemoveIndex, ItemLength, Aux, RemainingBytes;
	xTaskHandle		p;

	if( Queue == NULL )
		return 0;

#if			defined QUEUE_STRICT_CHRONOLOGY
	if( Queue->ItemsAvailable == 0 || Queue->ReadingOwner != NULL || !listLIST_IS_EMPTY( &Queue->TasksWaitingToRead ))
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
	if( Queue->ItemsAvailable == 0 )
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
		return 0;

	RemoveIndex	= Queue->RemoveIndex;
	ItemLength	= (unsigned short)Queue->QueueBuffer[ RemoveIndex ];
	if( ++RemoveIndex >= Queue->QueueLength )
		RemoveIndex	= 0;
	if( ItemLength > 127 )
		{
		ItemLength	= ( ItemLength & 0x7f ) | ( (unsigned short)Queue->QueueBuffer[ RemoveIndex ] << 7 );
		if( ++RemoveIndex >= Queue->QueueLength )
			RemoveIndex	= 0;
		}
	ItemLength++;

	if( BufferSize < ItemLength )
		return -1;

	RemainingBytes	= ItemLength;
	while(( Aux = min( RemainingBytes, Queue->QueueLength - RemoveIndex ) ) > 0 )
		{
		memcpy( (void*)Ptr, (void*)&Queue->QueueBuffer[ RemoveIndex ], Aux );
		Ptr	= (char*)Ptr + Aux;
		if(( RemoveIndex += Aux ) >= Queue->QueueLength )
			RemoveIndex	= 0;
		RemainingBytes	-= Aux;
		}

	Queue->RemoveIndex	= RemoveIndex;

	Queue->ItemsAvailable--;
	Queue->BytesFree	+= EffectiveSize( ItemLength );

	/*------------------------------------------------------------------------*/
	/*
	 We removed some bytes from the buffer, there should be room for more items
	 in the queue, check to see whether there is a task wanting to write and if
	 its item will fit in the buffer.
	*/
	/*------------------------------------------------------------------------*/
#if			defined QUEUE_STRICT_CHRONOLOGY
	if( Queue->WritingOwner == NULL && ( p = listGET_OWNER_OF_HEAD_ENTRY( &Queue->TasksWaitingToWrite )) != NULL
		&& EffectiveSize( (unsigned int)pvGetExtraParameter( p )) <= Queue->BytesFree )
		{
		Queue->WritingOwner	= p;
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
	if( !listLIST_IS_EMPTY( &Queue->TasksWaitingToWrite ))
		{
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */

		if( xTaskRemoveFromEventList( &Queue->TasksWaitingToWrite ) == pdTRUE )
			return ItemLength | 0x40000000;
		}

	return ItemLength;
	}
/*============================================================================*/
int xFlexiQueueWrite( flexiqueue_t *Queue, const void *Ptr, unsigned int ItemSize, portTickType  TimeToWait )
	{
	portTickType 		DeadLine;
	xTaskHandle			*p;
	unsigned int		InsertIndex, Aux, RemainingBytes;
	int					MustYield	= 0;

	if( Queue == NULL )
		return 0;

	if( EffectiveSize( ItemSize ) > Queue->QueueLength )
		return -1;

	portENTER_CRITICAL();

#if			defined QUEUE_STRICT_CHRONOLOGY
	if( EffectiveSize( ItemSize ) > Queue->BytesFree || Queue->WritingOwner != NULL || !listLIST_IS_EMPTY( &Queue->TasksWaitingToWrite ))
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
	if( EffectiveSize( ItemSize ) > Queue->BytesFree )
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
		{
		if( TimeToWait == 0 )
			{
			portEXIT_CRITICAL();
			return 0;
			}

		DeadLine	= xTaskGetTickCount() + TimeToWait;
		vSetExtraParameter( xTaskGetCurrentTaskHandle(), (void*)ItemSize );

#if			!defined QUEUE_STRICT_CHRONOLOGY
		do
#endif	/*	!defined QUEUE_STRICT_CHRONOLOGY */
			{
			vTaskPlaceOnEventList( &( Queue->TasksWaitingToWrite ), DeadLine );

	        taskYIELD();
	        }
#if			!defined QUEUE_STRICT_CHRONOLOGY
		while(( (signed long)TimeToWait < 0 || (signed long)( DeadLine - xTaskGetTickCount() ) > 0 ) && EffectiveSize( ItemSize ) > Queue->BytesFree );
#endif	/*	!defined QUEUE_STRICT_CHRONOLOGY */


#if			defined QUEUE_STRICT_CHRONOLOGY
		if( EffectiveSize( ItemSize ) > Queue->BytesFree || Queue->WritingOwner != xTaskGetCurrentTaskHandle() )
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
		if( EffectiveSize( ItemSize ) > Queue->BytesFree )
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
			{
			portEXIT_CRITICAL();
			return 0;
			}
		}

	Aux			= ItemSize - 1;
	InsertIndex	= Queue->InsertIndex;
	Queue->QueueBuffer[ InsertIndex ]	= ItemSize > 128 ? (unsigned char)( Aux | 0x80 ) : (unsigned char)( Aux & 0x7f );
	if( ++InsertIndex >= Queue->QueueLength )
		InsertIndex	= 0;
	if( ItemSize > 128 )
		{
		Queue->QueueBuffer[ InsertIndex ]	= (unsigned char)( Aux >> 7 );
		if( ++InsertIndex >= Queue->QueueLength )
			InsertIndex	= 0;
		}

	RemainingBytes	= ItemSize;
	while(( Aux = min( RemainingBytes, Queue->QueueLength - InsertIndex ) ) > 0 )
		{
		memcpy( (void*)&Queue->QueueBuffer[ InsertIndex ], (void*)Ptr, Aux );
		Ptr	= (char*)Ptr + Aux;
		if(( InsertIndex += Aux ) >= Queue->QueueLength )
			InsertIndex	= 0;
		RemainingBytes	-= Aux;
		}

	Queue->InsertIndex	= InsertIndex;
	
	Queue->ItemsAvailable++;
	Queue->BytesFree	-= EffectiveSize( ItemSize );

#if			defined QUEUE_STRICT_CHRONOLOGY
	Queue->WritingOwner		= NULL;

	if(( p = (xTaskHandle)listGET_OWNER_OF_HEAD_ENTRY( &Queue->TasksWaitingToWrite )) != NULL && EffectiveSize( (unsigned int)pvGetExtraParameter( p )) <= Queue->BytesFree )
		{
		Queue->WritingOwner	= p;
			
		if( xTaskRemoveFromEventList( &Queue->TasksWaitingToWrite ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IMMEDIATE ))
			MustYield	= 1;

		}
	/*------------------------------------------------------------------------*/
	/*
	 We inserted some bytes into the buffer, let's check to see whether there is
	 a task wanting to read them.
	*/
	/*------------------------------------------------------------------------*/
	if( Queue->ReadingOwner == NULL && ( p = listGET_OWNER_OF_HEAD_ENTRY( &Queue->TasksWaitingToRead )) != NULL )
		{
		/*
		 Let's be practical, waking up a task that doesn't have room in its buffer
		 to receive the next item just to deny it access to the item is not wise,
		 let's try to find a task lower in the list that has room for the item.
		*/
/*@@@@
		for( Aux = GetSizeOfNextItem( Queue ); p != NULL && (unsigned int)pvGetExtraParameter( p ) < Aux; p = p->Next )
			{}
		if( p != NULL )
@@@@*/
			{
			Queue->ReadingOwner	= p;
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
		if( !listLIST_IS_EMPTY( &Queue->TasksWaitingToRead ))
			{
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
			if( xTaskRemoveFromEventList( &Queue->TasksWaitingToWrite ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IMMEDIATE ))
				MustYield	= 1;
			}
#if			defined QUEUE_STRICT_CHRONOLOGY
		}
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */

	if( MustYield )
		taskYIELD();

	portEXIT_CRITICAL();
	return 1;
	}
/*============================================================================*/
int xFlexiQueueWriteFromISR( flexiqueue_t *Queue, const void *Ptr, unsigned int ItemSize )
	{
	xTaskHandle		*p;
	unsigned int	InsertIndex, Aux, RemainingBytes;

	if( Queue == NULL )
		return 0;

	if( EffectiveSize( ItemSize ) > Queue->QueueLength )
		return -1;

#if			defined QUEUE_STRICT_CHRONOLOGY
	if( EffectiveSize( ItemSize ) > Queue->BytesFree || Queue->WritingOwner != NULL || !listLIST_IS_EMPTY( &Queue->TasksWaitingToWrite ))
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
	if( EffectiveSize( ItemSize ) > Queue->BytesFree )
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
		return 0;

	RemainingBytes	= ItemSize;
	InsertIndex		= Queue->InsertIndex;
	while(( Aux = min( RemainingBytes, Queue->QueueLength - InsertIndex ) ) > 0 )
		{
		memcpy( (void*)&Queue->QueueBuffer[ InsertIndex ], (void*)Ptr, Aux );
		Ptr	= (char*)Ptr + Aux;
		if(( InsertIndex += Aux ) >= Queue->QueueLength )
			InsertIndex	= 0;
		RemainingBytes	-= Aux;
		}

	Queue->InsertIndex	= InsertIndex;
	
	Queue->ItemsAvailable++;
	Queue->BytesFree	-= EffectiveSize( ItemSize );

#if			defined QUEUE_STRICT_CHRONOLOGY
	/*------------------------------------------------------------------------*/
	/*
	 We inserted some bytes into the buffer, let's check to see whether there is
	 a task wanting to read them.
	*/
	/*------------------------------------------------------------------------*/
	if( Queue->ReadingOwner == NULL && ( p = listGET_OWNER_OF_HEAD_ENTRY( &Queue->TasksWaitingToRead )) != NULL )
		{
		/*
		 Let's be practical, waking up a task that doesn't have room in its buffer
		 to receive the next item just to deny it access to the item is not wise,
		 let's try to find a task lower in the list that has room for the item.
		*/
/*@@@@
		for( Aux = GetSizeOfNextItem( Queue ); p != NULL && (unsigned int)pvGetExtraParameter( p ) < Aux; p = p->Next )
			{}
		if( p != NULL )
@@@@*/
			{
			Queue->ReadingOwner	= p;
#else	/*	defined QUEUE_STRICT_CHRONOLOGY */
		if(!listLIST_IS_EMPTY( &Queue->TasksWaitingToRead ))
			{
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
			if( xTaskRemoveFromEventList( &Queue->TasksWaitingToRead ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IN_ISR ))
					return 2;
			}
#if			defined QUEUE_STRICT_CHRONOLOGY
		}
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */

	return 1;
	}
/*============================================================================*/
int xFlexiQueueFlush( flexiqueue_t *Queue, int Flag )
	{
	xTaskHandle			*p;
	int					MustYield	= 0;
	int					f = 0;

	if( Queue == NULL )
		return 0;

	portENTER_CRITICAL();

	Queue->ItemsAvailable	= 0;
	Queue->RemoveIndex		= 0;
	Queue->InsertIndex		= 0;
#if			defined QUEUE_STRICT_CHRONOLOGY
	Queue->ReadingOwner		= NULL;
	Queue->WritingOwner		= NULL;
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */
	Queue->BytesFree		= Queue->QueueLength;

	if( Flag & QUEUE_FLUSH_READING_TASKS )
		while( !listLIST_IS_EMPTY( &Queue->TasksWaitingToRead ))
			{
			f	|= QUEUE_FLUSH_READING_TASKS;
			if( xTaskRemoveFromEventList( &Queue->TasksWaitingToRead ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IMMEDIATE ))
				MustYield	= 1;
			}

	if( Flag & QUEUE_FLUSH_WRITING_TASKS )
		{
		while( !listLIST_IS_EMPTY( &Queue->TasksWaitingToWrite ))
			{
			f	|= QUEUE_FLUSH_WRITING_TASKS;
			if( xTaskRemoveFromEventList( &Queue->TasksWaitingToWrite ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IMMEDIATE ))
				MustYield	= 1;
			}
		}
#if			defined QUEUE_STRICT_CHRONOLOGY
	else if(( p = (xTaskHandle)listGET_OWNER_OF_HEAD_ENTRY( &Queue->TasksWaitingToWrite )) != NULL )
		{
		Queue->WritingOwner	= p;

		if( xTaskRemoveFromEventList( &Queue->TasksWaitingToWrite ) == pdTRUE && ( Queue->Mode & QUEUE_SWITCH_IMMEDIATE ))
			MustYield	= 1;
		}
#endif	/*	defined QUEUE_STRICT_CHRONOLOGY */

	if( MustYield )
		taskYIELD();

	portEXIT_CRITICAL();

	return f;
	}
/*============================================================================*/
