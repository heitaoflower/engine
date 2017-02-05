/**
 * @file
 */

#include "core/tests/AbstractTest.h"
#include "core/Set.h"
#include "core/Random.h"
#include <numeric>

namespace core {

class SetTest: public AbstractTest {
};


TEST_F(SetTest, testDiff) {
	std::unordered_set<int> set1;
	const int n = 1000;
	for (int i = 0; i < n; ++i) {
		set1.insert(i);
	}
	std::unordered_set<int> set2;
	for (int i = 0; i < n; ++i) {
		set2.insert(i);
	}
	set2.insert(n + 1);
	auto diff = core::setDifference(set1, set2);
	EXPECT_FALSE(diff.empty());
	EXPECT_EQ(1u, diff.size());
}

TEST_F(SetTest, testDiff2) {
	std::unordered_set<int> set1;
	const int n = 1000;
	for (int i = 0; i < n; ++i) {
		set1.insert(i);
		set1.insert(-n - i);
	}
	std::unordered_set<int> set2;
	for (int i = 0; i < n; ++i) {
		set2.insert(i);
		set2.insert(n + i);
	}
	auto diff = core::setDifference(set1, set2);
	EXPECT_FALSE(diff.empty());
	EXPECT_EQ(2000u, diff.size());
}

// exactly what is done for calculating the visible entities
TEST_F(SetTest, testVisibleActions) {
	std::unordered_set<int> set1;
	std::unordered_set<int> set2;

	set1.insert(1);
	set1.insert(2);
	set1.insert(3);

	set2.insert(1);
	set2.insert(4);
	set2.insert(5);
	set2.insert(6);

	const auto& inBoth = core::setIntersection(set1, set2);
	EXPECT_EQ(1u, inBoth.size());
	EXPECT_EQ(1, *inBoth.begin());
	const std::unordered_set<int>& removeFromSet2 = core::setDifference(inBoth, set2);
	EXPECT_EQ(3u, removeFromSet2.size());
	const std::unordered_set<int>& addToSet2 = core::setDifference(set1, inBoth);
	EXPECT_EQ(2u, addToSet2.size());
	set2 = core::setUnion(inBoth, addToSet2);
	EXPECT_EQ(3u, set2.size());
	EXPECT_EQ(inBoth.size() + addToSet2.size(), set2.size());
}

TEST_F(SetTest, testVisibleActionsPerformance) {
	std::unordered_set<int> set1;
	std::unordered_set<int> set2;

	for (int i = 0; i < 5000000; ++i) {
		set1.insert(i);
		set2.insert(i * 2);
	}

	const auto& inBoth = core::setIntersection(set1, set2);
	const std::unordered_set<int>& removeFromSet2 = core::setDifference(inBoth, set2);
	const std::unordered_set<int>& addToSet2 = core::setDifference(set1, inBoth);
	EXPECT_FALSE(addToSet2.empty());
	EXPECT_FALSE(removeFromSet2.empty());
}

TEST_F(SetTest, testVectorIntersectionSorted) {
	const int offset = 1000;
	std::vector<int> v1(5000000);
	std::vector<int> v2(v1.size());

	std::iota(std::begin(v1), std::end(v1), 0);
	std::iota(std::begin(v2), std::end(v2), offset);

	std::vector<int> out;
	core::vectorIntersection(v1, v2, out);
	EXPECT_EQ(v1.size() - offset, out.size());
}

TEST_F(SetTest, testVectorIntersectionUnsorted) {
	const int offset = 1000;
	std::vector<int> v1(5000000);
	std::vector<int> v2(v1.size());

	std::iota(std::begin(v1), std::end(v1), 0);
	std::iota(std::begin(v2), std::end(v2), offset);

	core::Random rnd;
	rnd.shuffle(v1.begin(), v1.end());
	rnd.shuffle(v2.begin(), v2.end());

	std::sort(v1.begin(), v1.end());
	std::sort(v2.begin(), v2.end());

	std::vector<int> out;
	core::vectorIntersection(v1, v2, out);
	EXPECT_EQ(v1.size() - offset, out.size());
}

}
