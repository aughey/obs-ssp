/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#include <util/platform.h>
#include "VFrameQueue.h"
#include <QDebug>
#include <obs-module.h>

VFrameQueue::VFrameQueue() {
    maxTime = 0;
}


void VFrameQueue::start() {
    running = true;
    pthread_create(&thread, nullptr, pthread_run, (void *)this);
}


void VFrameQueue::stop() {
    running = false;
    sem.release();
    pthread_join(thread, nullptr);
}


void VFrameQueue::setFrameCallback(VFrameQueue::CallbackFunc cb) {
    callback = std::move(cb);
}


void VFrameQueue::setFrameTime(uint64_t time_us) {
    maxTime = time_us;
}


void VFrameQueue::enqueue(imf::SspH264Data data, uint64_t time_us, bool noDrop) {
    uint8_t *copy_data = (uint8_t *)malloc(data.len);
    memcpy(copy_data, data.data, data.len);
    data.data = copy_data;
    QMutexLocker locker(&queueLock);
    frameQueue.enqueue({data, time_us, noDrop});
    sem.release();
}

void* VFrameQueue::pthread_run(void *q) {
    run((VFrameQueue *)q);
    return nullptr;
}

VFrameQueue::Frame VFrameQueue::dequeue(int* depth) {
    queueLock.lock();
    if (!frameQueue.empty()) {
        VFrameQueue::Frame f = frameQueue.dequeue();
        queueLock.unlock();
        return f;
    }
    queueLock.unlock();

    sem.acquire();
    QMutexLocker locker(&queueLock);
    if (frameQueue.empty()) {
        return Frame();
    }
    if (depth != 0) {
        *depth = frameQueue.size();
    }
    return frameQueue.dequeue();
}


void VFrameQueue::run(VFrameQueue *q) {
    Frame current;
    uint64_t lastFrameTime = 0, lastStartTime = 0, processingTime = 0;
    uint64_t lastprinttime = 0;

    // We just wait until we get the first frame
    while (true) {
        current = q->dequeue();
        if (current.data.data != 0) {
            break;
        }
    }

    lastStartTime = os_gettime_ns()/1000;
    q->callback(&current.data);
    lastFrameTime = current.time;
    processingTime = os_gettime_ns()/1000 - lastStartTime;
    free((void *)current.data.data);
    lastprinttime = os_gettime_ns() / 1000;

    while(q->running){
        int queue_depth;
        current = q->dequeue(&queue_depth);
        if (current.data.data == 0) {
            continue;
        }

       /* if (os_gettime_ns() / 1000 >= lastprinttime + 500000) {
            blog(LOG_WARNING, QString("Queue depth is at %1").arg(queue_depth).toStdString().c_str());
            lastprinttime = os_gettime_ns() / 1000;
        }*/

        bool process_frame = true;
        if (current.noDrop) {
            process_frame = true;
        } else if (current.time < lastFrameTime) {
            blog(LOG_WARNING, QString("Dropping frame because frame time %1 is less than the last frame time %2").arg(current.time).arg(lastFrameTime).toStdString().c_str());
            process_frame = false;
        }
        /*  else if (current.time - lastFrameTime + 15000 > processingTime) {
            blog(LOG_WARNING, QString("Dropped frame at %1 processing time %2").arg(current.time - lastFrameTime).arg(processingTime).toStdString().c_str());
            process_frame = false;
        }*/
        else if (queue_depth > 10) {
            blog(LOG_WARNING, QString("Dropped frame at %1 because queue depth was %2").arg(current.time - lastFrameTime).arg(queue_depth).toStdString().c_str());
            process_frame = false;
        }
        else {
            process_frame = true;
        }

        lastStartTime = os_gettime_ns() / 1000;
        lastFrameTime = current.time;
        if(process_frame) {
            q->callback(&current.data);
            processingTime = os_gettime_ns()/1000 - lastStartTime;
        }
        free((void *)current.data.data);
    }
}



