#pragma once

float readBatteryVoltage();            // 返回实际电池电压（单位 V）
int getBatteryPercentage(float voltage); // 由已读电压估算电量（0 ~ 100%），避免重复采样
int getBatteryPercentage();            // 便捷封装：内部自读一次电压
