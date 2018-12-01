#pragma once
/*
* Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
*  http://aws.amazon.com/apache2.0
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/
#include <aws/crt/Exports.h>

#include <aws/crt/Types.h>

#include <aws/common/date_time.h>

#include <chrono>

namespace Aws
{
    namespace Crt
    {
        enum class DateFormat
        {
            RFC822 = AWS_DATE_FORMAT_RFC822,
            ISO_8601 = AWS_DATE_FORMAT_ISO_8601,
            AutoDetect = AWS_DATE_FORMAT_AUTO_DETECT,
        };

        enum class Month
        {
            January = AWS_DATE_MONTH_JANUARY,
            February = AWS_DATE_MONTH_FEBRUARY,
            March = AWS_DATE_MONTH_MARCH,
            April = AWS_DATE_MONTH_APRIL,
            May = AWS_DATE_MONTH_MAY,
            June = AWS_DATE_MONTH_JUNE,
            July = AWS_DATE_MONTH_JULY,
            August = AWS_DATE_MONTH_AUGUST,
            September = AWS_DATE_MONTH_SEPTEMBER,
            October = AWS_DATE_MONTH_OCTOBER,
            November = AWS_DATE_MONTH_NOVEMBER,
            December = AWS_DATE_MONTH_DECEMBER,
        };

        enum class DayOfWeek
        {
            Sunday = AWS_DATE_DAY_OF_WEEK_SUNDAY,
            Monday = AWS_DATE_DAY_OF_WEEK_MONDAY,
            Tuesday = AWS_DATE_DAY_OF_WEEK_TUESDAY,
            Wednesday = AWS_DATE_DAY_OF_WEEK_WEDNESDAY,
            Thursday = AWS_DATE_DAY_OF_WEEK_THURSDAY,
            Friday = AWS_DATE_DAY_OF_WEEK_FRIDAY,
            Saturday = AWS_DATE_DAY_OF_WEEK_SATURDAY,
        };

        class AWS_CRT_CPP_API DateTime final
        {
        public:
            /**
             *  Initializes time point to epoch
             */
            DateTime();

            /**
            *  Initializes time point to any other arbirtrary timepoint
            */
            DateTime(const std::chrono::system_clock::time_point& timepointToAssign);

            /**
             * Initializes time point to millis Since epoch
             */
            DateTime(uint64_t millisSinceEpoch);

            /**
             * Initializes time point to epoch time in seconds.millis
             */
            DateTime(double epoch_millis);

            /**
             * Initializes time point to value represented by timestamp and format.
             */
            DateTime(const char* timestamp, DateFormat format);

            bool operator == (const DateTime& other) const;
            bool operator < (const DateTime& other) const;
            bool operator > (const DateTime& other) const;
            bool operator != (const DateTime& other) const;
            bool operator <= (const DateTime& other) const;
            bool operator >= (const DateTime& other) const;

            DateTime operator+(const std::chrono::milliseconds& a) const;
            DateTime operator-(const std::chrono::milliseconds& a) const;

            /**
             * Assign from seconds.millis since epoch.
             */
            DateTime& operator=(double secondsSinceEpoch);

            /**
             * Assign from millis since epoch.
             */
            DateTime& operator=(uint64_t millisSinceEpoch);

            /**
            * Assign from another time_point
            */
            DateTime& operator=(const std::chrono::system_clock::time_point& timepointToAssign);

            /**
             * Assign from an ISO8601 or RFC822 formatted string
             */
            DateTime& operator=(const char* timestamp);

            operator bool();
            int GetLastError();

            /**
             * Convert dateTime to local time string using predefined format.
             */
            bool ToLocalTimeString(DateFormat format, ByteBuf& outputBuf) const;

            /**
            * Convert dateTime to GMT time string using predefined format.
            */
            bool ToGmtString(DateFormat format, ByteBuf& outputBuf) const;

            /**
             * Get the representation of this datetime as seconds.milliseconds since epoch
             */
            double SecondsWithMSPrecision() const;

            /**
             * Milliseconds since epoch of this datetime.
             */
            uint64_t Millis() const;

            /**
             *  In the likely case this class doesn't do everything you need to do, here's a copy of the time_point structure. Have fun.
             */
            std::chrono::system_clock::time_point UnderlyingTimestamp() const;

            /**
             * Get the Year portion of this dateTime. localTime if true, return local time, otherwise return UTC
             */
            uint16_t GetYear(bool localTime = false) const;

            /**
            * Get the Month portion of this dateTime. localTime if true, return local time, otherwise return UTC
            */
            Month GetMonth(bool localTime = false) const;

            /**
            * Get the Day of the Month portion of this dateTime. localTime if true, return local time, otherwise return UTC
            */
            uint8_t GetDay(bool localTime = false) const;

            /**
            * Get the Day of the Week portion of this dateTime. localTime if true, return local time, otherwise return UTC
            */
            DayOfWeek GetDayOfWeek(bool localTime = false) const;

            /**
            * Get the Hour portion of this dateTime. localTime if true, return local time, otherwise return UTC
            */
            uint8_t GetHour(bool localTime = false) const;

            /**
            * Get the Minute portion of this dateTime. localTime if true, return local time, otherwise return UTC
            */
            uint8_t GetMinute(bool localTime = false) const;

            /**
            * Get the Second portion of this dateTime. localTime if true, return local time, otherwise return UTC
            */
            uint8_t GetSecond(bool localTime = false) const;

            /**
            * Get whether or not this dateTime is in Daylight savings time. localTime if true, return local time, otherwise return UTC
            */
            bool IsDST(bool localTime = false) const;

            /**
             * Get an instance of DateTime representing this very instant.
             */
            static DateTime Now();

            /**
             * Get the millis since epoch representing this very instant.
             */
            static uint64_t CurrentTimeMillis();

            /**
             * Calculates the current hour of the day in localtime.
             */
            static uint8_t CalculateCurrentHour();

            /**
             * The amazon timestamp format is a double with seconds.milliseconds
             */
            static double ComputeCurrentTimestampSecondsMillis();

            /**
             * Compute the difference between two timestamps.
             */
            static std::chrono::milliseconds Diff(const DateTime& a, const DateTime& b);

            std::chrono::milliseconds operator - (const DateTime& other) const;

        private:
            aws_date_time m_date_time;
            bool m_good;
        };
    }
}
