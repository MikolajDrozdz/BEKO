"""Bootstrap script — creates the initial admin user.

Usage:
    cd backend
    python seed.py
    python seed.py --username admin --password secretpass
"""
import argparse
import sys
from database import SessionLocal, engine, Base
import models
from auth import hash_password

Base.metadata.create_all(bind=engine)


def seed(username: str, password: str) -> None:
    db = SessionLocal()
    try:
        existing = db.query(models.User).filter(models.User.username == username).first()
        if existing:
            print(f"User '{username}' already exists (role={existing.role}). Skipping.")
            return

        admin = models.User(
            username=username,
            hashed_password=hash_password(password),
            role="admin",
            is_active=True,
        )
        admin.permissions = models.ALL_CAPABILITIES + ["users"]
        db.add(admin)
        db.commit()
        print(f"Admin user '{username}' created successfully.")
    finally:
        db.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Seed initial admin user")
    parser.add_argument("--username", default="admin", help="Admin username (default: admin)")
    parser.add_argument("--password", default="admin123", help="Admin password (default: admin123)")
    args = parser.parse_args()

    if args.password == "admin123":
        print("WARNING: Using default password 'admin123'. Change it after first login.")

    seed(args.username, args.password)
    sys.exit(0)
