# UE Debug — Epic GitHub CL Search (Optional)

**Issue**: #6002, #10452
**Parent Skill**: [SKILL.md](SKILL.md)
**Loading**: Reference material — only consult when the symptom mentions a specific UE version OR when source-level investigation has located the buggy code area but not the exact mechanism.

---

## Epic GitHub CL Search (Optional — for version-specific bugs)

When the question mentions a **specific UE version** (e.g., "UE 5.6.1") or describes a bug that
may have been **fixed in a newer version**, search Epic GitHub for fix CLs using the question's keywords.

**When to use** (any one):
- Question mentions a specific UE version AND a bug/regression
- Source reading (2-hop) found the code area but not the exact bug mechanism
- The symptom sounds like a known engine regression

```bash
# Step 1: Search using keywords FROM THE QUESTION (never from GT or prior knowledge)
gh api search/commits --method GET \
  -f "q=<2-3 keywords from question> repo:EpicGames/UnrealEngine" \
  --jq '.items[:5] | .[] | {sha: .sha[:12], date: .commit.author.date, message: .commit.message[:200]}'

# Step 2: If a relevant fix CL is found, read the changed files
gh api repos/EpicGames/UnrealEngine/commits/<sha> \
  --jq '{message: .commit.message[:300], files: [.files[:10] | .[] | {filename, additions, deletions}]}'

# Step 3: ⚠️ READ THE ACTUAL DIFF — this is where the root cause details live
# The fix patch shows exactly which function was buggy and how it was fixed.
# Filter to the most relevant file and read its patch content.
gh api repos/EpicGames/UnrealEngine/commits/<sha> \
  --jq '.files[] | select(.filename | contains("<keyword from question>")) | {filename, patch: .patch[:500]}'
```

**When to read the diff** (Step 3):
- The commit message mentions a fix for the exact symptom described in the question
- Step 2 shows changed files in a relevant engine module
- Your 2-hop source reading found the area but not the exact bug mechanism

**Rules**:
- Use ONLY keywords that appear in the user's question — never inject terms from other sources
- This is a supplementary step, not a replacement for source reading
- If no relevant CL found, skip and write the report with what you have
- Limit to 3 tool calls (1 search + 1 detail + 1 diff read)

---

**See also**: For deeper Epic-source archaeology (CL detail / file history / commit-message search / crash tracking / tree walk / branch diff), use the dedicated [`/ue-cl-tracker`](../ue-cl-tracker/SKILL.md) skill.
