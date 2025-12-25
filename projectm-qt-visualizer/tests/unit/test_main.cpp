/**
 * @file test_main.cpp
 * @brief Test suite entry point using Qt Test.
 */
#include <QtTest>
#include <QCoreApplication>
// Include test classes here as they're implemented
// #include "core/test_Logger.cpp"
int main(int argc, char *argv[])
{
QCoreApplication app(argc, argv);
qDebug() << "Test suite placeholder - add tests as features are implemented";
qDebug() << "I use Arch, BTW - even for testing.";
return 0;
}
