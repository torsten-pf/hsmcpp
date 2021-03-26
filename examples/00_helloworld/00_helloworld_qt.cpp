#include <unistd.h>
#include <hsmcpp/hsm.hpp>
#include <hsmcpp/HsmEventDispatcherQt.hpp>
#include <QCoreApplication>

enum class States
{
    OFF,
    ON
};

enum class Events
{
    SWITCH
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    HierarchicalStateMachine<States, Events> hsm(States::OFF);

    hsm.initialize(std::make_shared<HsmEventDispatcherQt>());

    hsm.registerState(States::OFF, [&hsm](const VariantList_t& args)
    {
        printf("Off\n");
        usleep(1000000);
        hsm.transition(Events::SWITCH);
    });
    hsm.registerState(States::ON, [&hsm](const VariantList_t& args)
    {
        printf("On\n");
        usleep(1000000);
        hsm.transition(Events::SWITCH);
    });

    hsm.registerTransition(States::OFF, States::ON, Events::SWITCH);
    hsm.registerTransition(States::ON, States::OFF, Events::SWITCH);

    hsm.transition(Events::SWITCH);

    app.exec();

    return 0;
}