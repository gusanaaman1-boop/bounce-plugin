#include "Harness.h"

namespace test
{
std::vector<Case>& registry()  { static std::vector<Case> r; return r; }

namespace { int passed = 0, failed = 0; const Case* current = nullptr; }

void check (bool ok, const juce::String& what, const char* file, int line)
{
    if (ok)
    {
        ++passed;
        return;
    }

    ++failed;
    std::cout << "  FAIL  [" << (current != nullptr ? current->name : "?") << "] " << what
              << "   (" << juce::File (file).getFileName() << ":" << line << ")\n";
}

void note (const juce::String& text)
{
    std::cout << "        " << text << "\n";
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String filter = argc > 1 ? juce::String (argv[1]) : juce::String();

    for (const auto& c : test::registry())
    {
        if (filter.isNotEmpty() && ! juce::String (c.suite).containsIgnoreCase (filter)
                                && ! juce::String (c.name).containsIgnoreCase (filter))
            continue;

        test::current = &c;
        const auto before = test::failed;
        std::cout << "[" << c.suite << "] " << c.name << "\n";
        c.fn();
        if (test::failed == before)
            std::cout << "        ok\n";
    }

    std::cout << "\n" << test::passed << " checks passed, " << test::failed << " failed\n";
    return test::failed == 0 ? 0 : 1;
}
