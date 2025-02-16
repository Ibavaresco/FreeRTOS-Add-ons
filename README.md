This code is outdated, it was written for a very old version of FreeRTOS. Most probably it will need to be updated to work with current versions.

The FlexiQueue is a queue implementation that has internal memory management and can be used to transfer data of variable length, from one byte up to the size of the queue's buffer (minus one or two bytes).
Each element in the queue can be of a different size. The smaller the data, the more elements can be placed in the queue.

The mutex implementation is a real mutex, where only the task that owns the mutex can give it back, differently than with FreeRTOS's original implementation.
