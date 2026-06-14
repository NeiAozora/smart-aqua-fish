<?php

namespace App\Console\Commands;

use App\Helpers\IotHelper;
use Illuminate\Console\Command;

class IotHeartbeatCommand extends Command
{
    protected $signature = 'iot:heartbeat';
    protected $description = 'Run IOT heartbeat processing for auto detection and commands';

    public function handle()
    {
        $this->info('Starting IoT Heartbeat...');
        IotHelper::processHeartbeat();
        $this->info('IoT Heartbeat completed at ' . now());
        return Command::SUCCESS;
    }
}
