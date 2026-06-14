<?php

use Illuminate\Support\Facades\Schedule;

Schedule::command('iot:heartbeat')->everySecond();
