#!/usr/bin/env bash
# sync.sh —— 双机同步脚本（公司 WSL 与家里 Ubuntu 共用）
#
# 用法: scripts/sync.sh push | pull
#   push: 把本机已提交的内容推到 GitHub（晚上备份工作，不自动提交未提交的改动）
#   pull: 从 GitHub 拉取另一台机器的提交（--rebase --autostash，早上同步）
# 由 crontab 定时调用；日志统一写在 ~/ros2_sync.log
set -u

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$HOME/ros2_sync.log"
MODE="${1:-}"

log() { echo "[$(date '+%F %T')] [$MODE] $*" >> "$LOG_FILE"; }

cd "$REPO_DIR" || { log "无法进入仓库目录 $REPO_DIR"; exit 1; }

if [ "$MODE" = "push" ]; then
  # 只推已提交的内容；有未提交改动时记一笔提醒，绝不自动 git add/commit
  if [ -n "$(git status --porcelain)" ]; then
    log "注意：有 $(git status --porcelain | wc -l) 个文件未提交（不会被推送），请记得 commit"
  fi
  out=$(git push 2>&1 | tail -3)
  rc=${PIPESTATUS[0]}
  echo "$out" >> "$LOG_FILE"
  if [ "$rc" -eq 0 ]; then
    log "push 成功"
  else
    log "push 失败（可能远端有家里机器的新提交），明早 pull --rebase 后再试"
  fi
elif [ "$MODE" = "pull" ]; then
  out=$(git pull --rebase --autostash 2>&1 | tail -5)
  rc=${PIPESTATUS[0]}
  echo "$out" >> "$LOG_FILE"
  if [ "$rc" -eq 0 ]; then
    log "pull --rebase 成功"
  else
    log "pull 失败：可能存在冲突，需手动解决（git status 查看；git rebase --abort 可放弃）"
  fi
else
  log "用法错误：应为 sync.sh push 或 sync.sh pull"
  exit 1
fi
