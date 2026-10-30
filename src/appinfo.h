#pragma once
// Название, автор и версия — в одном месте
#define AQ_NAME       "AutoQ"
#define AQ_AUTHOR     "Qisit"
#define AQ_VERSION_NUMBER "1.18.3 beta"   // номер x.y.z; «beta» — пока программа в бете
// Тестовая сборка не для релиза (-DAQ_TEST_BUILD=ON): «… test». Автообновление считает её
// старее релиза с тем же номером, и она обновится до него сама
#ifdef AQ_TEST_BUILD
#define AQ_VERSION    AQ_VERSION_NUMBER " test"
#else
#define AQ_VERSION    AQ_VERSION_NUMBER
#endif
